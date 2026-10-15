#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SubObjects/Collision.h"
#include "NSlib/Physics/PhysicsScene.h"

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

    void ActorList::RebuildFromJson(const nlohmann::json& scene, Scene& owner, const ObjectFactoryFn& factory)
    {
        // Clear が断った後に組むと古い配置物の後ろへ新しい配置物が並ぶので、組む前にここで断る
        if (m_updating)
        {
            NS_LOG_ERROR(Scene, "ActorList::RebuildFromJson: 段の更新の最中に呼ばれたので組み直しを断った");
            return;
        }

        // 一時オブジェクトも捨てる。組み直す JSON に戻り先が無い
        Clear();

        // カウンタは 1 始まりでファイルの id を知らない。読込値まで上げないと次に置く 1 個目が既存とぶつかる
        const nlohmann::json& objects = SceneJsonObjects(scene);
        m_nextObjectId = std::max(m_nextObjectId, SceneJsonNextObjectId(scene));
        // 手編集でカウンタが既存 id より小さいファイルもあるので配置物の id の最大を見る
        for (const nlohmann::json& entry : objects)
        {
            if (ObjectJsonId(entry) >= m_nextObjectId)
            {
                m_nextObjectId = ObjectJsonId(entry) + 1;
            }
        }

        m_objects.reserve(objects.size());

        // 配置物の組み立ては呼出側の知識。ファクトリの無い起動前 / テストでは何も組まない
        if (factory)
        {
            // 先に全 object を組んで、開始は後段でまとめて行う
            // OnStart で ActorRef を解決する SubObject が、自分より後ろの object も引けるようにするため
            for (const nlohmann::json& entry : objects)
            {
                std::unique_ptr<SceneActor> obj = factory(entry);
                if (!obj)
                {
                    continue; // 組み立てる SubObject が無いオブジェクトはファクトリが nullptr を返す
                }

                obj->AttachScene(&owner);
                ApplyIdentity(*obj, entry);
                m_objects.push_back(std::move(obj));
            }

            MarkIndexDirty();
            for (std::unique_ptr<SceneActor>& objPtr : m_objects)
            {
                objPtr->OnStart();
            }
            // 全ての配置物が開始してから、揃っている前提の用意をさせる
            for (std::unique_ptr<SceneActor>& objPtr : m_objects)
            {
                objPtr->InitAfterPlacement();
            }
        }

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        SnapshotObjects();
    }

    SceneActor* ActorList::Append(std::unique_ptr<SceneActor> obj)
    {
        if (!obj)
        {
            return nullptr;
        }
        SceneActor* raw = obj.get();
        m_objects.push_back(std::move(obj));
        MarkIndexDirty();
        return raw;
    }

    SceneActor* ActorList::AppendWithNewId(std::unique_ptr<SceneActor> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }
        obj->SetId(AllocateObjectId());

        // ファイルの参照は名前で書くので、プレイ中に足す物も既存と重ならない名前にする
        RenameObject(*obj, name);
        return Append(std::move(obj));
    }

    void ActorList::RemoveByObjectId(NS::uint32 objectId)
    {
        // 段の最中に解放すると、その段で後に呼ぶ予定の生ポインタが破棄済みになる
        if (m_updating)
        {
            NS_LOG_ERROR(
                Scene, "ActorList::RemoveByObjectId: 段の更新の最中に呼ばれたので id {} の解放を断った", objectId);
            return;
        }

        // 0 は未採番の印。一時オブジェクトは id を持たないので、素通しすると先頭の一時が消える
        if (objectId == k_NoObjectId)
        {
            return;
        }

        for (std::vector<std::unique_ptr<SceneActor>>::iterator it = m_objects.begin(); it != m_objects.end(); ++it)
        {
            if ((*it)->Id() != objectId)
            {
                continue;
            }

            (*it)->OnEndPlay();
            MarkIndexDirty();
            m_objects.erase(it);
            return;
        }
    }

    void ActorList::RemoveDestroyed()
    {
        if (m_updating)
        {
            NS_LOG_ERROR(Scene,
                         "ActorList::RemoveDestroyed: 段の更新の最中に呼ばれたので Destroy した個体の解放を断った");
            return;
        }
        const std::size_t removed = std::erase_if(m_objects, [](const std::unique_ptr<SceneActor>& obj) {
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
        }
    }

    SceneActor* ActorList::ObjectAt(std::size_t index) const noexcept
    {
        if (index >= m_objects.size())
        {
            return nullptr;
        }
        return m_objects[index].get();
    }

    SceneActor* ActorList::FindByObjectId(NS::uint32 objectId) noexcept
    {
        // 0 は未採番の印。一時オブジェクトは id を持たないので、素通しすると先頭の一時が引ける
        if (objectId == k_NoObjectId)
        {
            return nullptr;
        }

        if (m_indexDirty)
        {
            for (std::unique_ptr<SceneActor>& obj : m_objects)
            {
                if (obj->Id() != k_NoObjectId)
                {
                    m_index.emplace(obj->Id(), obj.get());
                }
            }
            m_indexDirty = false;
        }

        const std::unordered_map<NS::uint32, SceneActor*>::iterator it = m_index.find(objectId);
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

    SceneActor* ActorList::FindObject(ActorRef ref) noexcept
    {
        return FindByObjectId(ref.id);
    }

    void ActorList::SyncPhysics(NS::Phys::PhysicsScene& physics)
    {
        for (const std::unique_ptr<SceneActor>& actor : m_objects)
        {
            Collision* collision = actor->CollisionSubObj();
            if (collision == nullptr)
            {
                continue;
            }
            if (collision->IsActive())
            {
                collision->SyncToPhysics();
            }
            else
            {
                collision->RemoveFromPhysics();
            }
        }
        physics.OptimizeBroadPhase();
    }

    void ActorList::SnapshotObjects()
    {
        for (std::unique_ptr<SceneActor>& obj : m_objects)
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
        for (const std::unique_ptr<SceneActor>& obj : m_objects)
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

    void ActorList::ApplyIdentity(SceneActor& obj, const nlohmann::json& entry)
    {
        obj.SetId(ObjectJsonId(entry));
        obj.SetName(std::string{ObjectJsonName(entry)});
        if (ObjectJsonActive(entry))
        {
            obj.Appear();
        }
        else
        {
            obj.Disappear();
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
        ApplyIdentity(*obj, entry);
        // 名前はシーンの中で一意に保つ。組み直さずに 1 体だけ入れるので、読込の一意化を通らない
        RenameObject(*obj, obj->Name());
        // 入れた物の id より先へカウンタを進める。進めないと次に置く 1 個目と重なる
        m_nextObjectId = std::max(m_nextObjectId, obj->Id() + 1);

        SceneActor* raw = obj.get();
        index = std::min(index, m_objects.size());
        m_objects.insert(m_objects.begin() + static_cast<std::ptrdiff_t>(index), std::move(obj));
        MarkIndexDirty();
        return raw;
    }

    std::size_t ActorList::IndexOfObjectId(NS::uint32 objectId) const noexcept
    {
        if (objectId == k_NoObjectId)
        {
            return m_objects.size();
        }
        for (std::size_t i = 0; i < m_objects.size(); ++i)
        {
            if (m_objects[i]->Id() == objectId)
            {
                return i;
            }
        }
        return m_objects.size();
    }

    void ActorList::RenameObject(SceneActor& obj, std::string_view name)
    {
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<SceneActor>& existing : m_objects)
        {
            if (existing.get() != &obj)
            {
                used.insert(existing->Name());
            }
        }
        obj.SetName(MakeUniqueActorName(name, used));
    }

    void ActorList::Clear()
    {
        if (m_updating)
        {
            NS_LOG_ERROR(Scene, "ActorList::Clear: 段の更新の最中に呼ばれたので配置物の解放を断った");
            return;
        }

        // OnEndPlay は生成の逆順で呼ぶ。依存し合う SubObject の後始末を生成と対称にする
        for (std::vector<std::unique_ptr<SceneActor>>::reverse_iterator it = m_objects.rbegin(); it != m_objects.rend();
             ++it)
        {
            (*it)->OnEndPlay();
        }
        MarkIndexDirty();
        m_objects.clear();
    }

} // namespace NS
