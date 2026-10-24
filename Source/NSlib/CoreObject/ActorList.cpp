#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace NS
{
    ActorList::ActorList() = default;
    ActorList::~ActorList()
    {
        Clear();
    }

    void ActorList::RebuildFromJson(const nlohmann::json& scene, Scene& owner, const ActorFactoryFn& factory)
    {
        // Clear が断った後に組むと古いアクターの後ろへ新しいアクターが並ぶので、組む前にここで断る
        if (m_updating)
        {
            NS_LOG_ERROR(Scene, "ActorList::RebuildFromJson: 段の更新の最中に呼ばれたので組み直しを断った");
            return;
        }

        Clear();

        // カウンタは 1 始まりでファイルの id を知らない。読込値まで上げないと次に置く 1 個目が既存とぶつかる
        const nlohmann::json& objects = SceneJsonActors(scene);
        m_nextActorId = std::max(m_nextActorId, NextFreeActorId(scene));

        m_actors.reserve(objects.size());

        // アクターの組み立ては呼出側の知識。ファクトリの無い起動前 / テストでは何も組まない
        if (factory)
        {
            // 先に全 object を組んで、開始は後段でまとめて行う
            // OnStart で ActorRef を解決する部品が、自分より後ろの object も引けるようにするため
            for (const nlohmann::json& entry : objects)
            {
                std::unique_ptr<SceneActor> obj = factory(entry);
                if (!obj)
                {
                    continue; // 組み立てる部品が無いオブジェクトはファクトリが nullptr を返す
                }

                // 部品はシーンの仕組みを OnInit で受け取り済み。別のシーンで作った物を入れると、その仕組みを指したまま動く。
                // Init を通ったアクターは一覧に入れないと OnEndPlay が呼ばれないので、断らずに止める
                NS_ASSERT(Scene,
                          obj->OwningScene() == &owner,
                          "ActorList::RebuildFromJson: {} はこのシーンで作られていない",
                          obj->ClassName());
                ApplyIdentity(obj.get(), ReadActorHeader(entry));
                m_actors.push_back(std::move(obj));
            }

            MarkIndexDirty();
            for (std::unique_ptr<SceneActor>& objPtr : m_actors)
            {
                objPtr->OnStart();
            }
            // 全てのアクターが開始してから、揃っている前提の用意をさせる
            for (std::unique_ptr<SceneActor>& objPtr : m_actors)
            {
                objPtr->InitAfterPlacement();
            }
        }
    }

    SceneActor* ActorList::AppendWithNewId(std::unique_ptr<SceneActor> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }
        obj->SetId(AllocateActorId());

        // ファイルの参照は名前で書くので、プレイ中に足す物も既存と重ならない名前にする
        RenameActor(obj.get(), name);
        SceneActor* raw = obj.get();
        m_actors.push_back(std::move(obj));
        MarkIndexDirty();
        ResolveActorRefs();
        return raw;
    }

    void ActorList::RemoveByActorId(uint32 objectId)
    {
        // 段の最中に解放すると、その段で後に呼ぶ予定の生ポインタが破棄済みになる
        if (m_updating)
        {
            NS_LOG_ERROR(
                Scene, "ActorList::RemoveByActorId: 段の更新の最中に呼ばれたので id {} の解放を断った", objectId);
            return;
        }

        // 0 は未採番を表す値。id の無い JSON から入れたアクターは 0 のままなので、素通しするとその 1 体が消える
        if (objectId == k_NoActorId)
        {
            return;
        }

        for (std::vector<std::unique_ptr<SceneActor>>::iterator it = m_actors.begin(); it != m_actors.end(); ++it)
        {
            if ((*it)->Id() != objectId)
            {
                continue;
            }

            (*it)->OnEndPlay();
            MarkIndexDirty();
            m_actors.erase(it);
            ResolveActorRefs();
            return;
        }
    }

    DetachedActor ActorList::DetachByActorId(uint32 objectId)
    {
        // 段の最中に外すと、その段で後に呼ぶ予定の生ポインタが一覧の外を指す
        if (m_updating)
        {
            NS_LOG_ERROR(
                Scene, "ActorList::DetachByActorId: 段の更新の最中に呼ばれたので id {} を外すのを断った", objectId);
            return DetachedActor{};
        }

        const std::size_t index = IndexOfActorId(objectId);
        if (index >= m_actors.size())
        {
            return DetachedActor{};
        }

        DetachedActor detached;
        detached.index = index;
        detached.wasAppeared = m_actors[index]->IsAppeared();
        m_actors[index]->Disappear();
        detached.actor = std::move(m_actors[index]);
        m_actors.erase(m_actors.begin() + static_cast<std::ptrdiff_t>(index));
        MarkIndexDirty();
        ResolveActorRefs();
        return detached;
    }

    SceneActor* ActorList::Reattach(DetachedActor detached)
    {
        if (!detached.actor)
        {
            return nullptr;
        }
        NS_ASSERT(Scene,
                  FindByActorId(detached.actor->Id()) == nullptr,
                  "id {} のアクターが一覧に居るのに、外した物を戻そうとしている",
                  detached.actor->Id());

        SceneActor* raw = detached.actor.get();
        const std::size_t index = std::min(detached.index, m_actors.size());
        m_actors.insert(m_actors.begin() + static_cast<std::ptrdiff_t>(index), std::move(detached.actor));
        MarkIndexDirty();
        ResolveActorRefs();
        if (detached.wasAppeared)
        {
            raw->Appear();
        }
        return raw;
    }

    void ActorList::RemoveDestroyed()
    {
        if (m_updating)
        {
            NS_LOG_ERROR(Scene,
                         "ActorList::RemoveDestroyed: 段の更新の最中に呼ばれたので Destroy した個体の解放を断った");
            return;
        }
        const std::size_t removed = std::erase_if(m_actors, [](const std::unique_ptr<SceneActor>& obj) {
            if (!obj->IsPendingDestroy())
            {
                return false;
            }
            obj->OnEndPlay();
            return true;
        });
        if (removed > 0)
        {
            MarkIndexDirty();
            ResolveActorRefs();
        }
    }

    SceneActor* ActorList::ActorAt(std::size_t index) const noexcept
    {
        if (index >= m_actors.size())
        {
            return nullptr;
        }
        return m_actors[index].get();
    }

    SceneActor* ActorList::FindByActorId(uint32 objectId) noexcept
    {
        // 0 は未採番を表す値。id の無い JSON から入れたアクターは 0 のままなので、素通しするとその 1 体が引ける
        if (objectId == k_NoActorId)
        {
            return nullptr;
        }

        if (m_indexDirty)
        {
            for (std::unique_ptr<SceneActor>& obj : m_actors)
            {
                if (obj->Id() != k_NoActorId)
                {
                    m_index.emplace(obj->Id(), obj.get());
                }
            }
            m_indexDirty = false;
        }

        const std::unordered_map<uint32, SceneActor*>::iterator it = m_index.find(objectId);
        if (it == m_index.end())
        {
            return nullptr;
        }
        return it->second;
    }

    void ActorList::MarkIndexDirty() noexcept
    {
        m_index.clear();
        m_indexDirty = true;
    }

    void ActorList::ResolveActorRefs()
    {
        for (const std::unique_ptr<SceneActor>& actor : m_actors)
        {
            actor->ResolveActorRefs();
        }
    }

    SceneActor* ActorList::FindActor(ActorRef ref) noexcept
    {
        return FindByActorId(ref.id);
    }

    void ActorList::SyncCollisions()
    {
        for (const std::unique_ptr<SceneActor>& actor : m_actors)
        {
            Collision* collision = actor->CollisionClassObj();
            if (collision != nullptr && actor->IsAppeared())
            {
                collision->SyncToPhysics(actor->Root().WorldMatrix(), actor.get());
            }
        }
    }

    void ActorList::SnapshotActors()
    {
        for (std::unique_ptr<SceneActor>& obj : m_actors)
        {
            obj->SnapshotForInterpolation();
        }
    }

    void ActorList::BeginPhase() noexcept
    {
        NS_ASSERT(Scene, !m_updating, "段の更新を入れ子で呼んでいる");
        m_updating = true;
    }

    void ActorList::EndPhase() noexcept
    {
        m_updating = false;
    }

    void ActorList::ExecutePhase(UpdatePhase phase)
    {
        NS_ASSERT(Scene, m_updating, "BeginPhase の前に段を回している");
        m_scheduled.clear();
        for (const std::unique_ptr<SceneActor>& obj : m_actors)
        {
            if (!obj->IsAppeared())
            {
                continue;
            }
            if (phase == UpdatePhase::Input || phase == UpdatePhase::RenderPrep || obj->Phase() == phase)
            {
                m_scheduled.push_back(obj.get());
            }
        }
        for (SceneActor* actor : m_scheduled)
        {
            // 解放の口は段の間は断るので、積んだ生ポインタは生きている。途中で消えた物だけ飛ばす
            if (!actor->IsAppeared())
            {
                continue;
            }
            if (phase == UpdatePhase::Input)
            {
                actor->ReadInput();
            }
            else if (phase == UpdatePhase::RenderPrep)
            {
                actor->PrepareRender();
            }
            else
            {
                actor->Update();
            }
        }
    }

    void ActorList::ApplyIdentity(SceneActor* obj, const ActorHeader& header)
    {
        NS_ASSERT(Scene, obj != nullptr, "ActorList::ApplyIdentity: 写す先のアクターが無い");
        obj->SetId(header.id);
        obj->m_actorFileName = header.actorFile;
        RenameActor(obj, header.name);
        if (header.active)
        {
            obj->Appear();
        }
        else
        {
            obj->Disappear();
        }
    }

    SceneActor* ActorList::InsertFromJson(std::unique_ptr<SceneActor> obj,
                                          const nlohmann::json& entry,
                                          std::size_t index)
    {
        if (!obj)
        {
            return nullptr;
        }
        // 組み直さずに 1 体だけ入れるので、読込の一意化を通らない。名前の一意は ApplyIdentity が保つ
        ApplyIdentity(obj.get(), ReadActorHeader(entry));
        // 入れた物の id より先へカウンタを進める。進めないと次に置く 1 個目と重なる
        m_nextActorId = std::max(m_nextActorId, obj->Id() + 1);

        SceneActor* raw = obj.get();
        index = std::min(index, m_actors.size());
        m_actors.insert(m_actors.begin() + static_cast<std::ptrdiff_t>(index), std::move(obj));
        MarkIndexDirty();
        ResolveActorRefs();
        return raw;
    }

    std::size_t ActorList::IndexOfActorId(uint32 objectId) const noexcept
    {
        if (objectId == k_NoActorId)
        {
            return m_actors.size();
        }
        for (std::size_t i = 0; i < m_actors.size(); ++i)
        {
            if (m_actors[i]->Id() == objectId)
            {
                return i;
            }
        }
        return m_actors.size();
    }

    void ActorList::RenameActor(SceneActor* obj, std::string_view name)
    {
        NS_ASSERT(Scene, obj != nullptr, "ActorList::RenameActor: 名前を変えるアクターが無い");
        std::unordered_set<std::string> used;
        used.reserve(m_actors.size());
        for (const std::unique_ptr<SceneActor>& existing : m_actors)
        {
            if (existing.get() != obj)
            {
                used.insert(existing->Name());
            }
        }
        obj->SetName(MakeUniqueActorName(name, used));
    }

    void ActorList::Clear()
    {
        if (m_updating)
        {
            NS_LOG_ERROR(Scene, "ActorList::Clear: 段の更新の最中に呼ばれたのでアクターの解放を断った");
            return;
        }

        // OnEndPlay は生成の逆順で呼ぶ。依存し合う部品の後始末を生成と対称にする
        for (std::vector<std::unique_ptr<SceneActor>>::reverse_iterator it = m_actors.rbegin(); it != m_actors.rend();
             ++it)
        {
            (*it)->OnEndPlay();
        }
        MarkIndexDirty();
        m_actors.clear();
    }

} // namespace NS
