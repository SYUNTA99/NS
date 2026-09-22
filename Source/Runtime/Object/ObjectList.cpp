#include "Runtime/Object/ObjectList.h"

#include "Runtime/Core/Assert.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace NS::Obj
{
    ObjectList::ObjectList() = default;
    ObjectList::~ObjectList() = default;

    void ObjectList::Rebuild(const SceneData& data, Scene& scene, const ObjectFactoryFn& factory)
    {
        // 実行時の一時オブジェクトはデータ由来でないため、退避して組み直し後も残す
        std::vector<std::unique_ptr<GameObject>> transients;
        for (auto& obj : m_objects)
        {
            if (obj->IsTransient())
            {
                transients.push_back(std::move(obj));
            }
        }
        std::erase_if(m_objects, [](const std::unique_ptr<GameObject>& obj) { return obj == nullptr; });

        Clear();

        // カウンタは 1 始まりでファイルの id を知らない。読込値まで上げないと次に置く 1 個目が既存とぶつかる
        m_nextObjectId = std::max(m_nextObjectId, data.nextObjectId);
        // 手編集でカウンタが既存 id より小さいファイルもあるので object 側の最大も見る
        for (const ObjectData& entry : data.objects)
        {
            if (entry.objectId >= m_nextObjectId)
            {
                m_nextObjectId = entry.objectId + 1;
            }
        }

        m_objects.reserve(data.objects.size() + transients.size());

        // 配置物の組み立ては呼出側の知識。ファクトリの無い起動前 / テストでは何も組まない
        if (factory)
        {
            // 先に全 object を組んで、開始は後段でまとめて行う
            // OnStart で ObjectRef を解決する component が、自分より後ろの object も引けるようにするため
            for (const ObjectData& entry : data.objects)
            {
                auto obj = factory(entry);
                if (!obj)
                {
                    continue; // 組み立てる component が無いオブジェクトはファクトリが nullptr を返す
                }

                obj->AttachScene(&scene);
                obj->SetId(entry.objectId);
                obj->SetName(entry.name);
                obj->SetActive(entry.active);

                m_objects.push_back(std::move(obj));
            }

            // 親子は全 object が揃ってから結ぶ。子が親より前に並ぶファイルでも同じ形に組める
            // 循環は SetParent が輪を閉じる結び付けを拒むので、手編集のデータでも組み上がる
            // id 引きを線形で回すと体数の二乗に効くので、組み立ての間だけ使う対応表で引く
            // 索引として持ち越さないのは、所有リストと同期を保つ手間を抱え込まないため
            std::unordered_map<std::uint32_t, GameObject*> byObjectId;
            byObjectId.reserve(m_objects.size());
            for (auto& obj : m_objects)
                byObjectId.emplace(obj->Id(), obj.get());

            const auto find = [&byObjectId](std::uint32_t id) -> GameObject* {
                const auto it = byObjectId.find(id);
                if (it == byObjectId.end())
                {
                    return nullptr;
                }
                return it->second;
            };

            for (const ObjectData& entry : data.objects)
            {
                if (entry.parentId == k_NoObjectId)
                {
                    continue;
                }
                GameObject* child = find(entry.objectId);
                GameObject* parent = find(entry.parentId);
                if (child != nullptr && parent != nullptr && child != parent)
                {
                    child->SetParent(parent);
                }
            }

            for (auto& objPtr : m_objects)
                objPtr->OnStart();
        }

        // 退避した一時オブジェクトを末尾へ戻す。開始済みなので OnStart は呼ばない
        for (auto& obj : transients)
        {
            m_objects.push_back(std::move(obj));
        }

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        SnapshotObjects();
    }

    GameObject* ObjectList::Append(std::unique_ptr<GameObject> obj)
    {
        if (!obj)
        {
            return nullptr;
        }
        GameObject* raw = obj.get();
        m_objects.push_back(std::move(obj));
        return raw;
    }

    void ObjectList::RemoveByObjectId(std::uint32_t objectId)
    {
        // 0 は未採番の印。一時オブジェクトは id を持たないので、素通しすると先頭の一時が消える
        if (objectId == k_NoObjectId)
        {
            return;
        }

        for (auto it = m_objects.begin(); it != m_objects.end(); ++it)
        {
            if ((*it)->Id() != objectId)
            {
                continue;
            }

            (*it)->OnEndPlay();
            m_objects.erase(it);
            return;
        }
    }

    GameObject* ObjectList::ObjectAt(std::size_t index) const noexcept
    {
        if (index >= m_objects.size())
        {
            return nullptr;
        }
        return m_objects[index].get();
    }

    GameObject* ObjectList::FindByObjectId(std::uint32_t objectId) noexcept
    {
        // 0 は未採番の印。一時オブジェクトは id を持たないので、素通しすると先頭の一時が引ける
        if (objectId == k_NoObjectId)
        {
            return nullptr;
        }

        for (auto& obj : m_objects)
        {
            if (obj->Id() == objectId)
            {
                return obj.get();
            }
        }
        return nullptr;
    }

    GameObject* ObjectList::FindObject(ObjectRef ref) noexcept
    {
        return FindByObjectId(ref.id);
    }

    void ObjectList::SyncPhysics(NS::Phys::PhysicsScene& physics)
    {
        // PhysicsScene を作り直さず、collider ごとに既存 body の shape と姿勢を同期する
        ForEachComponent<Collider>([&physics](Collider& collider) {
            if (collider.IsActive())
            {
                collider.SyncToPhysics(physics);
            }
            else
            {
                collider.RemoveFromPhysics(physics);
            }
        });
        physics.OptimizeBroadPhase();
    }

    void ObjectList::UpdateAllObjects()
    {
        UpdateObjects(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    }

    void ObjectList::SnapshotObjects()
    {
        for (auto& obj : m_objects)
        {
            obj->Root().Snapshot();
        }
    }

    void ObjectList::UpdateObjects(int firstPriority, int lastPriority)
    {
        // 入れ子で呼ぶと内側の clear が外側の並びを消し、下の範囲 for が無効なイテレータを辿る
        NS_ASSERT(Scene, !m_updating, "ObjectList::UpdateObjects を入れ子で呼んでいる");
        m_updating = true;

        // 帯の昇順で配置物を横断して回すため、範囲内の component を一度集めて priority で並べ直す
        // stable_sort なので同じ帯の中は配置物の並び順に落ちる
        // clear は容量を残すので毎フレームの確保が要らない
        m_scheduled.clear();
        for (auto& obj : m_objects)
        {
            for (Component* comp : obj->Components())
            {
                if (comp == nullptr)
                {
                    continue;
                }
                if (comp->Priority() >= firstPriority && comp->Priority() < lastPriority)
                {
                    m_scheduled.push_back(comp);
                }
            }
        }
        std::stable_sort(m_scheduled.begin(), m_scheduled.end(), [](const Component* a, const Component* b) noexcept {
            return a->Priority() < b->Priority();
        });

        // active はこの場で見る。先に回った component が後ろを SetActive(false) にしても効く
        for (Component* comp : m_scheduled)
        {
            if (comp->IsActive())
            {
                comp->OnUpdate();
            }
        }

        m_updating = false;
    }

    void ObjectList::Clear()
    {
        // OnEndPlay は生成の逆順で呼ぶ。依存し合う component の後始末を生成と対称にする
        for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
        {
            (*it)->OnEndPlay();
        }
        m_objects.clear();
    }

    std::vector<ObjectRefLocation> FindReferencesTo(const ObjectList& objects, std::uint32_t targetId)
    {
        std::vector<ObjectRefLocation> result;
        if (targetId == k_NoObjectId)
        {
            return result;
        }
        for (const GameObject* objPtr : objects)
        {
            const auto& components = objPtr->Components();
            for (std::size_t c = 0; c < components.size(); ++c)
            {
                const Component* comp = components[c];
                if (comp == nullptr)
                {
                    continue;
                }
                const ReflectionInfo* info = comp->GetReflection();
                if (info == nullptr)
                {
                    continue;
                }
                for (std::size_t f = 0; f < info->fieldCount; ++f)
                {
                    const FieldDesc& field = info->fields[f];
                    if (field.type != FieldType::ObjectRef)
                    {
                        continue;
                    }
                    ObjectRef value{};
                    field.get(comp, &value);
                    if (value.id != targetId)
                    {
                        continue;
                    }
                    result.push_back(ObjectRefLocation{objPtr->Id(), c, field.name});
                }
            }
        }
        return result;
    }

} // namespace NS::Obj
