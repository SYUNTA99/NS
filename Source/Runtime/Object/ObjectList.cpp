#include "Runtime/Object/ObjectList.h"

#include "Runtime/Core/Assert.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/ObjectName.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace NS::Obj
{
    ObjectList::ObjectList() = default;
    ObjectList::~ObjectList()
    {
        Clear();
    }

    void ObjectList::Rebuild(const nlohmann::json& scene, Scene& owner, const ObjectFactoryFn& factory)
    {
        // 実行時の一時オブジェクトはデータ由来でないため、退避して組み直し後も残す
        std::vector<std::unique_ptr<Actor>> transients;
        for (std::unique_ptr<Actor>& obj : m_objects)
        {
            if (obj->IsTransient())
            {
                transients.push_back(std::move(obj));
            }
        }
        std::erase_if(m_objects, [](const std::unique_ptr<Actor>& obj) { return obj == nullptr; });

        Clear();

        // カウンタは 1 始まりでファイルの id を知らない。読込値まで上げないと次に置く 1 個目が既存とぶつかる
        const nlohmann::json& objects = SceneJsonObjects(scene);
        m_nextObjectId = std::max(m_nextObjectId, SceneJsonNextObjectId(scene));
        // 手編集でカウンタが既存 id より小さいファイルもあるので object と component の最大も見る
        for (const nlohmann::json& entry : objects)
        {
            if (ObjectJsonId(entry) >= m_nextObjectId)
            {
                m_nextObjectId = ObjectJsonId(entry) + 1;
            }
        }

        m_objects.reserve(objects.size() + transients.size());

        // 配置物の組み立ては呼出側の知識。ファクトリの無い起動前 / テストでは何も組まない
        if (factory)
        {
            // 先に全 object を組んで、開始は後段でまとめて行う
            // OnStart で ActorRef を解決する component が、自分より後ろの object も引けるようにするため
            for (const nlohmann::json& entry : objects)
            {
                std::unique_ptr<Actor> obj = factory(entry);
                if (!obj)
                {
                    continue; // 組み立てる component が無いオブジェクトはファクトリが nullptr を返す
                }

                obj->AttachScene(&owner);
                ApplyIdentity(*obj, entry);
                m_objects.push_back(std::move(obj));
            }

            // 親子は全 object が揃ってから結ぶ。子が親より前に並ぶファイルでも同じ形に組める
            // 循環は SetParent が輪を閉じる結び付けを拒むので、手編集のデータでも組み上がる
            // id 引きを線形で回すと体数の二乗に効くので、組み立ての間だけ使う対応表で引く
            // 索引として持ち越さないのは、所有リストと同期を保つ手間を抱え込まないため
            std::unordered_map<std::uint32_t, Actor*> byObjectId;
            byObjectId.reserve(m_objects.size());
            for (std::unique_ptr<Actor>& obj : m_objects)
            {
                byObjectId.emplace(obj->Id(), obj.get());
            }

            const auto find = [&byObjectId](std::uint32_t id) -> Actor* {
                const std::unordered_map<std::uint32_t, Actor*>::iterator it = byObjectId.find(id);
                if (it == byObjectId.end())
                {
                    return nullptr;
                }
                return it->second;
            };

            for (const nlohmann::json& entry : objects)
            {
                if (ObjectJsonParent(entry) == k_NoObjectId)
                {
                    continue;
                }
                Actor* child = find(ObjectJsonId(entry));
                Actor* parent = find(ObjectJsonParent(entry));
                if (child != nullptr && parent != nullptr && child != parent)
                {
                    child->SetParent(parent);
                }
            }

            // 開始中に参照を引く component がいる。積み終えた並びで索引を作り直させる
            MarkIndexDirty();
            WarnMismatchedComponentRefs();
            for (std::unique_ptr<Actor>& objPtr : m_objects)
            {
                objPtr->OnStart();
            }
            // 全ての配置物が開始してから、揃っている前提の用意をさせる
            for (std::unique_ptr<Actor>& objPtr : m_objects)
            {
                objPtr->InitAfterPlacement();
            }
        }

        // 退避した一時オブジェクトを末尾へ戻す。開始済みなので OnStart は呼ばない
        for (std::unique_ptr<Actor>& obj : transients)
        {
            Actor* raw = obj.get();
            m_objects.push_back(std::move(obj));
            if (raw->IsActiveInHierarchy())
            {
                RegisterActor(raw);
            }
        }
        MarkIndexDirty();

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        SnapshotObjects();
    }

    Actor* ObjectList::Append(std::unique_ptr<Actor> obj)
    {
        if (!obj)
        {
            return nullptr;
        }
        Actor* raw = obj.get();
        m_objects.push_back(std::move(obj));
        MarkIndexDirty();
        if (raw->IsActiveInHierarchy())
        {
            RegisterActor(raw);
        }
        return raw;
    }

    Actor* ObjectList::AppendWithNewId(std::unique_ptr<Actor> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }
        obj->SetId(AllocateObjectId());
        // component も同じ空間から採番する。参照できる相手として配置物と同じ扱いにする

        // ファイルの参照は名前で書くので、プレイ中に足す物も既存と重ならない名前にする
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<Actor>& existing : m_objects)
        {
            used.insert(existing->Name());
        }
        obj->SetName(MakeUniqueObjectName(name, used));
        return Append(std::move(obj));
    }

    void ObjectList::RemoveByObjectId(std::uint32_t objectId)
    {
        // 0 は未採番の印。一時オブジェクトは id を持たないので、素通しすると先頭の一時が消える
        if (objectId == k_NoObjectId)
        {
            return;
        }

        for (std::vector<std::unique_ptr<Actor>>::iterator it = m_objects.begin(); it != m_objects.end(); ++it)
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

    void ObjectList::RemoveKilledTransients()
    {
        NS_ASSERT(Scene, !m_updating, "段の更新の最中に一時オブジェクトを捨てようとしている");
        const std::size_t removed = std::erase_if(m_objects, [](const std::unique_ptr<Actor>& obj) {
            if (!obj->IsTransient() || obj->IsAlive())
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

    Actor* ObjectList::ObjectAt(std::size_t index) const noexcept
    {
        if (index >= m_objects.size())
        {
            return nullptr;
        }
        return m_objects[index].get();
    }

    Actor* ObjectList::FindByObjectId(std::uint32_t objectId) noexcept
    {
        // 0 は未採番の印。一時オブジェクトは id を持たないので、素通しすると先頭の一時が引ける
        if (objectId == k_NoObjectId)
        {
            return nullptr;
        }

        if (m_indexDirty)
        {
            m_index.clear();
            for (std::unique_ptr<Actor>& obj : m_objects)
            {
                if (obj->Id() != k_NoObjectId)
                {
                    m_index.emplace(obj->Id(), obj.get());
                }
            }
            m_indexDirty = false;
        }

        const std::unordered_map<std::uint32_t, Actor*>::iterator it = m_index.find(objectId);
        if (it != m_index.end() && it->second->Id() == objectId)
        {
            return it->second;
        }

        // 索引を作った後で id を書き換えた配置物は索引とずれる。全体を見て索引を直す
        for (std::unique_ptr<Actor>& obj : m_objects)
        {
            if (obj->Id() == objectId)
            {
                m_index[objectId] = obj.get();
                return obj.get();
            }
        }
        return nullptr;
    }

    void ObjectList::MarkIndexDirty() noexcept
    {
        m_index.clear();
        m_indexDirty = true;
    }

    Actor* ObjectList::FindObject(ActorRef ref) noexcept
    {
        return FindByObjectId(ref.id);
    }

    void ObjectList::SyncPhysics(NS::Phys::PhysicsScene& physics)
    {
        for (const std::unique_ptr<Actor>& actor : m_objects)
        {
            actor->ForEachPart([&physics](std::string_view, Component& part) {
                if (Collider* collider = ComponentCast<Collider>(&part))
                {
                    if (collider->IsActive())
                    {
                        collider->SyncToPhysics(physics);
                    }
                    else
                    {
                        collider->RemoveFromPhysics(physics);
                    }
                }
            });
        }
        for (const std::unique_ptr<Actor>& actor : m_objects)
        {
            if (RigidBody* body = ComponentCast<RigidBody>(actor->Part("RigidBody")))
            {
                if (body->IsActive())
                {
                    body->SyncToPhysics(physics);
                }
                else
                {
                    body->RemoveFromPhysics(physics);
                }
            }
        }
        physics.OptimizeBroadPhase();
    }

    void ObjectList::SnapshotObjects()
    {
        for (std::unique_ptr<Actor>& obj : m_objects)
        {
            obj->Root().Snapshot();
        }
    }

    void ObjectList::RegisterActor(Actor* actor)
    {
        if (actor != nullptr && std::find(m_liveActors.begin(), m_liveActors.end(), actor) == m_liveActors.end())
        {
            m_liveActors.push_back(actor);
        }
    }

    void ObjectList::UnregisterActor(Actor* actor) noexcept
    {
        std::erase(m_liveActors, actor);
    }

    void ObjectList::ExecutePhase(UpdatePhase phase)
    {
        NS_ASSERT(Scene, !m_updating, "段の更新を入れ子で呼んでいる");
        m_updating = true;
        m_scheduled.clear();
        for (const TickerEntry& entry : m_tickers)
        {
            if (entry.priority == phase)
            {
                m_scheduled.push_back(ScheduledTick{.ticker = entry.ticker});
            }
        }
        for (const std::unique_ptr<Actor>& obj : m_objects)
        {
            if (std::find(m_liveActors.begin(), m_liveActors.end(), obj.get()) == m_liveActors.end())
            {
                continue;
            }
            if (phase == UpdatePhase::Input || phase == UpdatePhase::RenderPrep || obj->Phase() == phase)
            {
                m_scheduled.push_back(ScheduledTick{.actor = obj.get()});
            }
        }
        for (const ScheduledTick& tick : m_scheduled)
        {
            if (tick.actor != nullptr)
            {
                if (std::find(m_liveActors.begin(), m_liveActors.end(), tick.actor) == m_liveActors.end())
                {
                    continue;
                }
                if (!tick.actor->IsActiveInHierarchy())
                {
                    continue;
                }
                if (phase == UpdatePhase::Input)
                {
                    tick.actor->ReadInput();
                }
                else if (phase == UpdatePhase::RenderPrep)
                {
                    tick.actor->PrepareRender();
                }
                else
                {
                    tick.actor->Update();
                }
                continue;
            }
            const bool stillRegistered =
                std::any_of(m_tickers.begin(), m_tickers.end(), [&tick, phase](const TickerEntry& entry) noexcept {
                    return entry.ticker == tick.ticker && entry.priority == phase;
                });
            if (stillRegistered)
            {
                tick.ticker->OnTick();
            }
        }
        m_updating = false;
    }

    void ObjectList::AddTicker(ITickable* ticker, UpdatePhase priority)
    {
        if (ticker == nullptr)
        {
            return;
        }
        for (TickerEntry& entry : m_tickers)
        {
            if (entry.ticker == ticker)
            {
                entry.priority = priority;
                return;
            }
        }
        m_tickers.push_back(TickerEntry{.ticker = ticker, .priority = priority});
    }

    void ObjectList::RemoveTicker(ITickable* ticker) noexcept
    {
        std::erase_if(m_tickers, [ticker](const TickerEntry& entry) noexcept { return entry.ticker == ticker; });
    }

    Component* ObjectList::ResolvePart(ComponentRefValue ref) noexcept
    {
        if (!ref.IsSet())
        {
            return nullptr;
        }
        Actor* owner = FindObject(ref.actor);
        if (owner == nullptr)
        {
            return nullptr;
        }
        return owner->Part(ref.partName);
    }

    void ObjectList::WarnMismatchedComponentRefs()
    {
        for (const std::unique_ptr<Actor>& obj : m_objects)
        {
            obj->ForEachPart([this, &obj](std::string_view role, Component& part) {
                const ReflectionInfo* info = part.GetReflection();
                if (info == nullptr)
                {
                    return;
                }
                for (std::size_t i = 0; i < info->fieldCount; ++i)
                {
                    const FieldDesc& field = info->fields[i];
                    if (field.type != FieldType::ComponentRef || field.refType == nullptr)
                    {
                        continue;
                    }
                    ComponentRefValue value;
                    field.get(&part, &value);
                    const Component* target = ResolvePart(value);
                    if (target != nullptr && !target->IsA(field.refType()))
                    {
                        NS_LOG_WARN(Scene,
                                    "'{}' の {} の欄 '{}' が {} でない部品 '{}' を指している",
                                    obj->Name(),
                                    role,
                                    field.name,
                                    field.refType()->typeName,
                                    value.partName);
                    }
                }
            });
        }
    }

    void ObjectList::ApplyIdentity(Actor& obj, const nlohmann::json& entry)
    {
        obj.SetId(ObjectJsonId(entry));
        obj.SetName(std::string{ObjectJsonName(entry)});
        obj.SetActive(ObjectJsonActive(entry));
    }

    Actor* ObjectList::InsertFromJson(std::unique_ptr<Actor> obj, const nlohmann::json& entry, std::size_t index)
    {
        if (!obj)
        {
            return nullptr;
        }
        ApplyIdentity(*obj, entry);
        // 名前はシーンの中で一意に保つ。組み直さずに 1 体だけ入れるので、読込の一意化を通らない
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<Actor>& existing : m_objects)
        {
            used.insert(existing->Name());
        }
        obj->SetName(MakeUniqueObjectName(obj->Name(), used));
        // 入れた物の id より先へカウンタを進める。進めないと次に置く 1 個目と重なる
        m_nextObjectId = std::max(m_nextObjectId, obj->Id() + 1);

        Actor* raw = obj.get();
        index = std::min(index, m_objects.size());
        m_objects.insert(m_objects.begin() + static_cast<std::ptrdiff_t>(index), std::move(obj));
        MarkIndexDirty();
        return raw;
    }

    std::size_t ObjectList::IndexOfObjectId(std::uint32_t objectId) const noexcept
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

    void ObjectList::RenameObject(Actor& obj, std::string_view name)
    {
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<Actor>& existing : m_objects)
        {
            if (existing.get() != &obj)
            {
                used.insert(existing->Name());
            }
        }
        obj.SetName(MakeUniqueObjectName(name, used));
    }

    void ObjectList::Clear()
    {
        // OnEndPlay は生成の逆順で呼ぶ。依存し合う component の後始末を生成と対称にする
        for (std::vector<std::unique_ptr<Actor>>::reverse_iterator it = m_objects.rbegin(); it != m_objects.rend();
             ++it)
        {
            (*it)->OnEndPlay();
        }
        MarkIndexDirty();
        m_objects.clear();
        m_liveActors.clear();
    }

} // namespace NS::Obj
