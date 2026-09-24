#include "Runtime/Object/Scene/SceneData.h"

#include "Runtime/Object/Reflection/ComponentEntry.h"

#include <unordered_map>
#include <unordered_set>

namespace NS::Obj
{

    std::size_t FindObjectIndexById(const SceneData& scene, std::uint32_t id) noexcept
    {
        if (id == k_NoObjectId)
        {
            return k_NoObjectIndex;
        }
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            if (scene.objects[i].objectId == id)
                return i;
        }
        return k_NoObjectIndex;
    }

    void EnsureUniqueObjectIds(SceneData& scene)
    {
        // 先にカウンタを既存最大 id の先へ進め、これから振る id が既存と衝突しないようにする
        for (const ObjectData& object : scene.objects)
        {
            if (object.objectId >= scene.nextObjectId)
            {
                scene.nextObjectId = object.objectId + 1;
            }
        }

        // component の id も同じ空間なので、カウンタを進める段から一緒に見る
        for (const ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
            {
                continue;
            }
            for (const nlohmann::json& entry : object.components)
            {
                const std::uint32_t id = ComponentEntryId(entry);
                if (id >= scene.nextObjectId)
                {
                    scene.nextObjectId = id + 1;
                }
            }
        }

        // 未割当や重複は手編集・複製バグで入り得る。先勝ちで後続へ新 id を振る
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(scene.objects.size());
        for (ObjectData& object : scene.objects)
        {
            if (object.objectId == 0 || !seen.insert(object.objectId).second)
            {
                object.objectId = scene.nextObjectId++;
                seen.insert(object.objectId);
            }
        }

        // component も同じ表で見るので、object と component の間でも id が重ならない
        for (ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
            {
                continue;
            }
            for (nlohmann::json& entry : object.components)
            {
                const std::uint32_t id = ComponentEntryId(entry);
                if (id == 0 || !seen.insert(id).second)
                {
                    const std::uint32_t fresh = scene.nextObjectId++;
                    SetComponentEntryId(entry, fresh);
                    seen.insert(fresh);
                }
            }
        }

        EnsureUniqueObjectNames(scene);
    }

    void EnsureUniqueObjectNames(SceneData& scene)
    {
        // 付いている名前を先に全部押さえる。空の物へ先に番号を振ると、後ろの手書きの名前と重なる
        std::unordered_set<std::string> used;
        used.reserve(scene.objects.size());
        std::vector<ObjectData*> pending;
        for (ObjectData& object : scene.objects)
        {
            if (object.name.empty() || !used.insert(object.name).second)
            {
                pending.push_back(&object);
            }
        }
        for (ObjectData* object : pending)
        {
            object->name = MakeUniqueObjectName(object->name, used);
            used.insert(object->name);
        }

        // component の名前は配置物の中で一意にする。名前の無い古いデータは型名から付ける
        for (ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
            {
                continue;
            }
            std::unordered_set<std::string> usedComponentNames;
            usedComponentNames.reserve(object.components.size());
            std::vector<nlohmann::json*> pendingEntries;
            for (nlohmann::json& entry : object.components)
            {
                const std::string name{ComponentEntryName(entry)};
                if (name.empty() || !usedComponentNames.insert(name).second)
                {
                    pendingEntries.push_back(&entry);
                }
            }
            for (nlohmann::json* entry : pendingEntries)
            {
                std::string_view base = ComponentEntryName(*entry);
                if (base.empty())
                {
                    base = ComponentEntryType(*entry);
                }
                const std::string unique = MakeUniqueObjectName(base, usedComponentNames);
                SetComponentEntryName(*entry, unique);
                usedComponentNames.insert(unique);
            }
        }
    }

    std::size_t PruneDanglingObjectRefs(SceneData& scene)
    {
        // 配置物ごとに、持っている component の id を控える。ComponentRef は持ち主の中に居るかまで見る
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> componentIdsByObject;
        componentIdsByObject.reserve(scene.objects.size());
        for (const ObjectData& object : scene.objects)
        {
            std::unordered_set<std::uint32_t>& componentIds = componentIdsByObject[object.objectId];
            if (!object.components.is_array())
            {
                continue;
            }
            for (const nlohmann::json& entry : object.components)
            {
                componentIds.insert(ComponentEntryId(entry));
            }
        }

        std::size_t prunedCount = 0;
        for (ObjectData& object : scene.objects)
        {
            ForEachRefValue(object.components, [&componentIdsByObject, &prunedCount](nlohmann::json& value) {
                nlohmann::json& ref = value["ref"];
                if (!ref.is_number_unsigned())
                {
                    return;
                }
                const std::uint32_t objectId = ref.get<std::uint32_t>();
                const std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>>::const_iterator owner =
                    componentIdsByObject.find(objectId);
                const nlohmann::json::iterator componentIt = value.find("component");
                if (componentIt == value.end())
                {
                    // ObjectRef。未設定か、居る相手なら残す
                    if (objectId == k_NoObjectId || owner != componentIdsByObject.end())
                    {
                        return;
                    }
                    ref = 0u;
                    ++prunedCount;
                    return;
                }

                // ComponentRef。未設定か、持ち主の中に居る Component なら残す
                if (!componentIt->is_number_unsigned())
                {
                    return;
                }
                const std::uint32_t componentId = componentIt->get<std::uint32_t>();
                if (componentId == 0 && objectId == k_NoObjectId)
                {
                    return;
                }
                if (componentId != 0 && owner != componentIdsByObject.end() && owner->second.contains(componentId))
                {
                    return;
                }
                ref = 0u;
                *componentIt = 0u;
                ++prunedCount;
            });
        }
        return prunedCount;
    }

    void RemapObjectRefs(ObjectData& object, const std::unordered_map<std::uint32_t, std::uint32_t>& idMap)
    {
        ForEachRefValue(object.components, [&idMap](nlohmann::json& value) {
            for (const char* key : {"ref", "component"})
            {
                const nlohmann::json::iterator it = value.find(key);
                if (it == value.end() || !it->is_number_unsigned())
                {
                    continue;
                }
                const std::unordered_map<std::uint32_t, std::uint32_t>::const_iterator mapped =
                    idMap.find(it->get<std::uint32_t>());
                if (mapped != idMap.end())
                {
                    *it = mapped->second;
                }
            }
        });
    }

    std::size_t PruneInvalidParents(SceneData& scene)
    {
        std::unordered_map<std::uint32_t, std::size_t> indexById;
        indexById.reserve(scene.objects.size());
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            indexById.emplace(scene.objects[i].objectId, i);
        }

        std::size_t prunedCount = 0;
        for (ObjectData& object : scene.objects)
        {
            if (object.parentId == k_NoObjectId)
            {
                continue;
            }

            bool valid = object.parentId != object.objectId && indexById.contains(object.parentId);
            // 祖先を辿って自分へ戻れば循環。その場で root へ落とすので、輪の残りは正当な親子として通る
            std::uint32_t ancestor = object.parentId;
            for (std::size_t step = 0; valid && step < scene.objects.size(); ++step)
            {
                const std::unordered_map<std::uint32_t, std::size_t>::iterator it = indexById.find(ancestor);
                if (it == indexById.end())
                {
                    valid = false;
                    break;
                }
                ancestor = scene.objects[it->second].parentId;
                if (ancestor == k_NoObjectId)
                {
                    break;
                }
                if (ancestor == object.objectId)
                {
                    valid = false;
                }
            }

            if (!valid)
            {
                object.parentId = k_NoObjectId;
                ++prunedCount;
            }
        }
        return prunedCount;
    }

} // namespace NS::Obj
