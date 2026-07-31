#include "Runtime/Object/World.h"

#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace NS::Object
{
    World::World() = default;
    World::~World() = default;

    void World::Rebuild(const SceneData& data,
                        Scene& scene,
                        NS::Physics::PhysicsWorld& physics,
                        const ObjectFactoryFn& factory)
    {
        // 実行時の一時オブジェクトはデータ由来でないため、 退避して組み直しを生き残らせる
        std::vector<std::unique_ptr<GameObject>> transients;
        for (auto& obj : m_objects)
        {
            if (obj->IsTransient())
                transients.push_back(std::move(obj));
        }
        std::erase_if(m_objects, [](const std::unique_ptr<GameObject>& obj) { return obj == nullptr; });

        Clear();
        physics.Clear();

        // カウンタは 1 始まりでファイルの id を知らない。読込値まで上げないと次に置く 1 個目が既存とぶつかる
        m_nextObjectId = std::max(m_nextObjectId, data.nextObjectId);
        // 手編集でカウンタが既存 id より小さいファイルもあるので object 側の最大も見る
        for (const ObjectData& entry : data.objects)
        {
            if (entry.objectId >= m_nextObjectId)
                m_nextObjectId = entry.objectId + 1;
        }

        m_objects.reserve(data.objects.size() + transients.size());
        physics.ReserveAabbs(data.objects.size());

        // 並び順は object の持ち物なので、 配列の並びではなく order で組む
        // 同値は書かれた順のまま残すので、 order を持たない古いファイルは従来と同じ形に組み上がる
        std::vector<std::size_t> buildOrder(data.objects.size());
        std::iota(buildOrder.begin(), buildOrder.end(), std::size_t{0});
        std::stable_sort(buildOrder.begin(), buildOrder.end(), [&data](std::size_t a, std::size_t b) noexcept {
            return data.objects[a].order < data.objects[b].order;
        });

        // 配置物の組み立ては呼出側の知識。 ファクトリの無い起動前 / テストでは何も組まない
        if (factory)
        {
            // 先に全 object を組んで、 開始は後段でまとめて行う
            // OnStart で ObjectRef を解決する component が、 自分より後ろの object も引けるようにするため
            for (const std::size_t index : buildOrder)
            {
                const ObjectData& entry = data.objects[index];
                auto obj = factory(entry);
                if (!obj)
                    continue; // 組み立てる component が無いオブジェクトはファクトリが nullptr を返す

                obj->AttachScene(&scene);
                obj->SetId(entry.objectId);
                obj->SetName(entry.name);
                obj->SetOrder(entry.order);
                obj->SetActive(entry.active);

                m_objects.push_back(std::move(obj));
            }

            // 親子は全 object が揃ってから結ぶ。 子が親より前に並ぶファイルでも同じ形に組める
            // 循環は読込の PruneInvalidParents が落とし済みで、 ここへは届かない
            // id 引きを線形で回すと体数の二乗に効くので、 組み立ての間だけ生きる対応表で引く
            // 索引として持ち越さないのは、 所有リストと同期を保つ手間を抱え込まないため
            std::unordered_map<std::uint32_t, GameObject*> byObjectId;
            byObjectId.reserve(m_objects.size());
            for (auto& obj : m_objects)
                byObjectId.emplace(obj->Id(), obj.get());

            const auto find = [&byObjectId](std::uint32_t id) -> GameObject* {
                const auto it = byObjectId.find(id);
                if (it == byObjectId.end())
                    return nullptr;
                return it->second;
            };

            for (const std::size_t index : buildOrder)
            {
                const ObjectData& entry = data.objects[index];
                if (entry.parentId == k_NoObjectId)
                    continue;
                GameObject* child = find(entry.objectId);
                GameObject* parent = find(entry.parentId);
                if (child != nullptr && parent != nullptr && child != parent)
                    child->SetParent(parent);
            }

            for (auto& objPtr : m_objects)
                objPtr->OnStart();
        }

        // 退避した一時オブジェクトを末尾へ戻す。 開始済みなので OnStart は呼ばない
        for (auto& obj : transients)
            m_objects.push_back(std::move(obj));

        RebuildPhysics(physics);

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        for (auto& obj : m_objects)
            obj->Root().Snapshot();
    }

    GameObject* World::Append(std::unique_ptr<GameObject> obj)
    {
        if (!obj)
            return nullptr;
        GameObject* raw = obj.get();
        m_objects.push_back(std::move(obj));
        return raw;
    }

    void World::RemoveByObjectId(std::uint32_t objectId, NS::Physics::PhysicsWorld& physics)
    {
        for (auto it = m_objects.begin(); it != m_objects.end(); ++it)
        {
            if ((*it)->Id() != objectId)
                continue;

            (*it)->OnEndPlay();
            m_objects.erase(it);
            // 当たり箱は配置物と紐付かない平らな配列なので、 1 体分を抜くより張り直す
            RebuildPhysics(physics);
            return;
        }
    }

    GameObject* World::ObjectAt(std::size_t index) const noexcept
    {
        if (index >= m_objects.size())
            return nullptr;
        return m_objects[index].get();
    }

    GameObject* World::FindByObjectId(std::uint32_t objectId) noexcept
    {
        // 0 は未採番の印。 一時オブジェクトは id を持たないので、 素通しすると先頭の一時が引ける
        if (objectId == k_NoObjectId)
            return nullptr;
        for (auto& obj : m_objects)
            if (obj->Id() == objectId)
                return obj.get();
        return nullptr;
    }

    GameObject* World::FindObject(ObjectRef ref) noexcept
    {
        return FindByObjectId(ref.id);
    }

    void World::RebuildPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        // 当たりは Clear -> Add* -> BuildBroadphase で満たし直す。 古い当たりを残さない
        physics.Clear();
        physics.ReserveAabbs(m_objects.size());
        ForEachComponent<ColliderComponent>([&physics](const ColliderComponent& collider) {
            // 札の下りた component は当たりも持たない。 更新・ 描画と同じ問いで揃える
            if (collider.IsActive())
                collider.AddToPhysics(physics);
        });
        physics.BuildBroadphase();
    }

    void World::UpdateAllObjects()
    {
        UpdateObjects(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        SnapshotObjects();
    }

    void World::SnapshotObjects()
    {
        for (auto& obj : m_objects)
            obj->Root().Snapshot();
    }

    void World::UpdateObjects(int firstPriority, int lastPriority)
    {
        // 帯の昇順で世界中を回すため、 範囲内の component を一度集めて priority で並べ直す
        // stable_sort なので同じ帯の中は配置物の並び順に落ちる
        std::vector<Component*> scheduled;
        for (auto& obj : m_objects)
        {
            for (Component* comp : obj->Components())
            {
                if (comp == nullptr)
                    continue;
                if (comp->Priority() >= firstPriority && comp->Priority() < lastPriority)
                    scheduled.push_back(comp);
            }
        }
        std::stable_sort(scheduled.begin(), scheduled.end(), [](const Component* a, const Component* b) noexcept {
            return a->Priority() < b->Priority();
        });

        // 札はこの場で見る。 先に回った component が後ろを SetActive(false) で寝かせても効く
        for (Component* comp : scheduled)
        {
            if (comp->IsActive())
                comp->OnUpdate();
        }
    }

    void World::Clear()
    {
        // 配置物は生成の逆順で畳む。 依存し合う component の OnEndPlay 順序を生成時と対称に保つ
        for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
            (*it)->OnEndPlay();
        m_objects.clear();
    }

    std::vector<ObjectRefLocation> FindReferencesTo(const World& world, std::uint32_t targetId)
    {
        std::vector<ObjectRefLocation> result;
        if (targetId == k_NoObjectId)
            return result;
        for (const GameObject* objPtr : world)
        {
            const auto& components = objPtr->Components();
            for (std::size_t c = 0; c < components.size(); ++c)
            {
                const Component* comp = components[c];
                if (comp == nullptr)
                    continue;
                const ReflectionInfo* info = comp->GetReflection();
                if (info == nullptr)
                    continue;
                for (std::size_t f = 0; f < info->fieldCount; ++f)
                {
                    const FieldDesc& field = info->fields[f];
                    if (field.type != FieldType::ObjectRef)
                        continue;
                    ObjectRef value{};
                    field.get(comp, &value);
                    if (value.id != targetId)
                        continue;
                    result.push_back(ObjectRefLocation{objPtr->Id(), c, field.name});
                }
            }
        }
        return result;
    }

} // namespace NS::Object
