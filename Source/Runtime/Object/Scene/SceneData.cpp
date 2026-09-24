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

    std::string MakeUniqueObjectName(std::string_view base, const std::unordered_set<std::string>& used)
    {
        std::string name{"Object"};
        if (!base.empty())
        {
            name = std::string{base};
        }
        if (!used.contains(name))
        {
            return name;
        }
        for (std::uint32_t n = 1;; ++n)
        {
            std::string candidate = name + "_" + std::to_string(n);
            if (!used.contains(candidate))
            {
                return candidate;
            }
        }
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
    }

    std::size_t PruneDanglingObjectRefs(SceneData& scene)
    {
        std::unordered_set<std::uint32_t> validIds;
        validIds.reserve(scene.objects.size());
        for (const ObjectData& object : scene.objects)
        {
            validIds.insert(object.objectId);
        }

        std::size_t prunedCount = 0;
        for (ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
            {
                continue;
            }
            for (nlohmann::json& entry : object.components)
            {
                if (!entry.is_object())
                {
                    continue;
                }
                const auto fieldsIt = entry.find("fields");
                if (fieldsIt == entry.end() || !fieldsIt->is_object())
                {
                    continue;
                }
                for (auto& fieldValue : *fieldsIt)
                {
                    if (!fieldValue.is_object())
                    {
                        continue;
                    }
                    const auto refIt = fieldValue.find("ref");
                    if (refIt == fieldValue.end() || !refIt->is_number_unsigned())
                    {
                        continue;
                    }
                    const std::uint32_t id = refIt->get<std::uint32_t>();
                    if (id == k_NoObjectId || validIds.contains(id))
                    {
                        continue;
                    }
                    *refIt = 0u;
                    ++prunedCount;
                }
            }
        }
        return prunedCount;
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
                const auto it = indexById.find(ancestor);
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

    std::vector<ObjectRefLocation> FindReferencesTo(const SceneData& scene, std::uint32_t targetId)
    {
        std::vector<ObjectRefLocation> result;
        if (targetId == k_NoObjectId)
            return result;
        for (const ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
            {
                continue;
            }
            for (std::size_t c = 0; c < object.components.size(); ++c)
            {
                const nlohmann::json* fields = ComponentEntryFields(object.components[c]);
                if (fields == nullptr)
                {
                    continue;
                }
                for (auto it = fields->begin(); it != fields->end(); ++it)
                {
                    const nlohmann::json& fieldValue = it.value();
                    if (!fieldValue.is_object())
                    {
                        continue;
                    }
                    const auto refIt = fieldValue.find("ref");
                    if (refIt == fieldValue.end() || !refIt->is_number_unsigned())
                    {
                        continue;
                    }
                    if (refIt->get<std::uint32_t>() != targetId)
                    {
                        continue;
                    }
                    result.push_back(ObjectRefLocation{object.objectId, c, it.key()});
                }
            }
        }
        return result;
    }

} // namespace NS::Obj
