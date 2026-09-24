#include "Runtime/Object/ObjectList.h"

#include "Runtime/Core/Assert.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/ObjectName.h"
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
    ObjectList::~ObjectList() = default;

    void ObjectList::Rebuild(const nlohmann::json& scene, Scene& owner, const ObjectFactoryFn& factory)
    {
        // 実行時の一時オブジェクトはデータ由来でないため、退避して組み直し後も残す
        std::vector<std::unique_ptr<GameObject>> transients;
        for (std::unique_ptr<GameObject>& obj : m_objects)
        {
            if (obj->IsTransient())
            {
                transients.push_back(std::move(obj));
            }
        }
        std::erase_if(m_objects, [](const std::unique_ptr<GameObject>& obj) { return obj == nullptr; });

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
            for (const nlohmann::json& component : ObjectJsonComponents(entry))
            {
                const std::uint32_t componentId = ComponentEntryId(component);
                if (componentId >= m_nextObjectId)
                {
                    m_nextObjectId = componentId + 1;
                }
            }
        }

        m_objects.reserve(objects.size() + transients.size());

        // 配置物の組み立ては呼出側の知識。ファクトリの無い起動前 / テストでは何も組まない
        if (factory)
        {
            // 先に全 object を組んで、開始は後段でまとめて行う
            // OnStart で ObjectRef を解決する component が、自分より後ろの object も引けるようにするため
            for (const nlohmann::json& entry : objects)
            {
                std::unique_ptr<GameObject> obj = factory(entry);
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
            std::unordered_map<std::uint32_t, GameObject*> byObjectId;
            byObjectId.reserve(m_objects.size());
            for (std::unique_ptr<GameObject>& obj : m_objects)
                byObjectId.emplace(obj->Id(), obj.get());

            const auto find = [&byObjectId](std::uint32_t id) -> GameObject* {
                const std::unordered_map<std::uint32_t, GameObject*>::iterator it = byObjectId.find(id);
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
                GameObject* child = find(ObjectJsonId(entry));
                GameObject* parent = find(ObjectJsonParent(entry));
                if (child != nullptr && parent != nullptr && child != parent)
                {
                    child->SetParent(parent);
                }
            }

            // 開始中に参照を引く component がいる。積み終えた並びで索引を作り直させる
            MarkIndexDirty();
            WarnMismatchedComponentRefs();
            for (std::unique_ptr<GameObject>& objPtr : m_objects)
                objPtr->OnStart();
        }

        // 退避した一時オブジェクトを末尾へ戻す。開始済みなので OnStart は呼ばない
        for (std::unique_ptr<GameObject>& obj : transients)
        {
            m_objects.push_back(std::move(obj));
        }
        MarkIndexDirty();

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
        MarkIndexDirty();
        return raw;
    }

    GameObject* ObjectList::AppendWithNewId(std::unique_ptr<GameObject> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }
        obj->SetId(AllocateObjectId());
        // component も同じ空間から採番する。参照できる相手として配置物と同じ扱いにする
        for (Component* comp : obj->Components())
        {
            if (comp != nullptr)
            {
                comp->SetId(AllocateObjectId());
            }
        }
        // ファイルの参照は名前で書くので、プレイ中に足す物も既存と重ならない名前にする
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<GameObject>& existing : m_objects)
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

        for (std::vector<std::unique_ptr<GameObject>>::iterator it = m_objects.begin(); it != m_objects.end(); ++it)
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

        if (m_indexDirty)
        {
            m_index.clear();
            for (std::unique_ptr<GameObject>& obj : m_objects)
            {
                if (obj->Id() != k_NoObjectId)
                {
                    m_index.emplace(obj->Id(), obj.get());
                }
            }
            m_indexDirty = false;
        }

        const std::unordered_map<std::uint32_t, GameObject*>::iterator it = m_index.find(objectId);
        if (it != m_index.end() && it->second->Id() == objectId)
        {
            return it->second;
        }

        // 索引を作った後で id を書き換えた配置物は索引とずれる。全体を見て索引を直す
        for (std::unique_ptr<GameObject>& obj : m_objects)
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
        // collider の後に回す。RigidBody の形になった collider が自分の body を外し終えてから形を集める
        ForEachComponent<RigidBody>([&physics](RigidBody& body) {
            if (body.IsActive())
            {
                body.SyncToPhysics(physics);
            }
            else
            {
                body.RemoveFromPhysics(physics);
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
        for (std::unique_ptr<GameObject>& obj : m_objects)
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
        for (std::unique_ptr<GameObject>& obj : m_objects)
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

    Component* ObjectList::FindComponent(ComponentRefValue ref) noexcept
    {
        if (!ref.IsSet())
        {
            return nullptr;
        }
        GameObject* owner = FindObject(ObjectRef{ref.object});
        if (owner == nullptr)
        {
            return nullptr;
        }
        return owner->FindComponentById(ref.component);
    }

    void ObjectList::WarnMismatchedComponentRefs()
    {
        for (const std::unique_ptr<GameObject>& obj : m_objects)
        {
            for (const Component* comp : obj->Components())
            {
                const ReflectionInfo* info = comp->GetReflection();
                if (info == nullptr)
                {
                    continue;
                }
                for (std::size_t i = 0; i < info->fieldCount; ++i)
                {
                    const FieldDesc& field = info->fields[i];
                    if (field.type != FieldType::ComponentRef || field.refType == nullptr)
                    {
                        continue;
                    }
                    ComponentRefValue value{};
                    field.get(comp, &value);
                    const Component* target = FindComponent(value);
                    if (target != nullptr && !target->IsA(field.refType()))
                    {
                        NS_LOG_WARN(Scene,
                                    "'{}' の {} の欄 '{}' が {} でない '{}' を指している。引いても見つからない扱いになる",
                                    obj->Name(),
                                    comp->Name(),
                                    field.name,
                                    field.refType()->typeName,
                                    target->Name());
                    }
                }
            }
        }
    }

    void ObjectList::ApplyIdentity(GameObject& obj, const nlohmann::json& entry)
    {
        obj.SetId(ObjectJsonId(entry));
        obj.SetName(std::string{ObjectJsonName(entry)});
        obj.SetActive(ObjectJsonActive(entry));
        AssignComponentIds(obj, entry);
    }

    GameObject* ObjectList::InsertFromJson(std::unique_ptr<GameObject> obj, const nlohmann::json& entry, std::size_t index)
    {
        if (!obj)
        {
            return nullptr;
        }
        ApplyIdentity(*obj, entry);
        // 名前はシーンの中で一意に保つ。組み直さずに 1 体だけ入れるので、読込の一意化を通らない
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<GameObject>& existing : m_objects)
        {
            used.insert(existing->Name());
        }
        obj->SetName(MakeUniqueObjectName(obj->Name(), used));
        // 入れた物の id より先へカウンタを進める。進めないと次に置く 1 個目と重なる
        m_nextObjectId = std::max(m_nextObjectId, obj->Id() + 1);
        for (const Component* comp : obj->Components())
        {
            m_nextObjectId = std::max(m_nextObjectId, comp->Id() + 1);
        }

        GameObject* raw = obj.get();
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

    void ObjectList::RenameObject(GameObject& obj, std::string_view name)
    {
        std::unordered_set<std::string> used;
        used.reserve(m_objects.size());
        for (const std::unique_ptr<GameObject>& existing : m_objects)
        {
            if (existing.get() != &obj)
            {
                used.insert(existing->Name());
            }
        }
        obj.SetName(MakeUniqueObjectName(name, used));
    }

    void ObjectList::AssignComponentIds(GameObject& obj, const nlohmann::json& entry)
    {
        // 組み立てと同じ規則で件と実体を対応させる。規則を別に書くと、同じ型が 2 つある時に id が入れ違う
        const nlohmann::json& components = ObjectJsonComponents(entry);
        std::vector<Component*> taken;
        taken.reserve(components.size());
        for (const nlohmann::json& component : components)
        {
            Component* comp = MatchComponentEntry(obj, component, taken);
            if (comp == nullptr)
            {
                continue;
            }
            taken.push_back(comp);
            comp->SetId(ComponentEntryId(component));
        }
    }

    void ObjectList::Clear()
    {
        // OnEndPlay は生成の逆順で呼ぶ。依存し合う component の後始末を生成と対称にする
        for (std::vector<std::unique_ptr<GameObject>>::reverse_iterator it = m_objects.rbegin(); it != m_objects.rend(); ++it)
        {
            (*it)->OnEndPlay();
        }
        MarkIndexDirty();
        m_objects.clear();
    }

} // namespace NS::Obj
