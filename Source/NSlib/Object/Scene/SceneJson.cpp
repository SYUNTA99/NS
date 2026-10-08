#include "NSlib/Object/Scene/SceneJson.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectName.h"
#include "NSlib/Object/Reflection/ComponentEntry.h"
#include "NSlib/Windows/Filesystem.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NS::Obj
{
    namespace
    {
        //! 保存形式のバージョン。形式を変えたら上げ、読込は一致のみ受け付ける
        //! 3: リフレクション欄名を日本語化。旧欄名のファイルを黙って既定値で読まないための引き上げ
        //! 4: transform の回転を Euler 度 3 要素から クォータニオン 4 要素の 1 欄へ
        constexpr int k_FormatVersion = 5;

        //! 読込時の上限。巨大 size / 要素数による メモリ枯渇を防ぐ
        constexpr std::size_t k_MaxSceneFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t k_MaxObjectCount = 100'000u;

        //! ファイルの配置物 1 体を、知っている欄だけの形へ整える。未知の欄は捨て、fields の中身は素通し
        nlohmann::json NormalizeObject(const nlohmann::json& json)
        {
            nlohmann::json object = MakeObjectJson();
            if (!json.is_object())
            {
                return object;
            }
            SetObjectJsonId(object, ObjectJsonId(json));
            SetObjectJsonClass(object, ObjectJsonClass(json));
            SetObjectJsonName(object, ObjectJsonName(json));
            SetObjectJsonParent(object, ObjectJsonParent(json));
            SetObjectJsonActive(object, ObjectJsonActive(json));
            nlohmann::json& parts = ObjectJsonParts(object);
            const nlohmann::json& source = ObjectJsonParts(json);
            for (nlohmann::json::const_iterator part = source.begin(); part != source.end(); ++part)
            {
                if (part.value().is_object())
                {
                    parts[part.key()] = part.value();
                }
            }
            (void)EnsureTransformComponent(object);
            return object;
        }

        // ファイルの参照は相手の名前で書く。空と重複した名前は相手が 1 つに決まらないので id のまま残す
        void WriteRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonObjects(scene);
            std::unordered_map<std::string, int> counts;
            std::unordered_map<std::uint32_t, std::string> names;
            for (const nlohmann::json& object : objects)
            {
                ++counts[std::string{ObjectJsonName(object)}];
            }
            for (const nlohmann::json& object : objects)
            {
                const std::string name{ObjectJsonName(object)};
                if (!name.empty() && counts[name] == 1)
                {
                    names.emplace(ObjectJsonId(object), name);
                }
            }
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&names](nlohmann::json& value) {
                    nlohmann::json& ref = value["ref"];
                    if (!ref.is_number_unsigned())
                    {
                        return;
                    }
                    const std::unordered_map<std::uint32_t, std::string>::const_iterator found =
                        names.find(ref.get<std::uint32_t>());
                    if (found != names.end())
                    {
                        ref = found->second;
                    }
                });
            }
        }

        // ファイルの参照は相手の名前で書かれている。名前を一意にした後で id へ直す。旧形式の数値はそのまま通す
        void ReadRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonObjects(scene);
            std::unordered_map<std::string, std::uint32_t> ids;
            for (const nlohmann::json& object : objects)
            {
                ids.emplace(std::string{ObjectJsonName(object)}, ObjectJsonId(object));
            }
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&ids](nlohmann::json& value) {
                    nlohmann::json& ref = value["ref"];
                    if (!ref.is_string())
                    {
                        return;
                    }
                    const std::unordered_map<std::string, std::uint32_t>::const_iterator found =
                        ids.find(ref.get<std::string>());
                    if (found == ids.end())
                    {
                        ref = 0u;
                    }
                    else
                    {
                        ref = found->second;
                    }
                });
            }
        }

        nlohmann::json& EnsureEnvironment(nlohmann::json& scene)
        {
            if (!scene.is_object())
            {
                scene = MakeSceneJson();
            }
            nlohmann::json& environment = scene["environment"];
            if (!environment.is_object())
            {
                environment = nlohmann::json::object();
            }
            return environment;
        }
    } // namespace

    nlohmann::json MakeSceneJson()
    {
        nlohmann::json scene = nlohmann::json::object();
        scene["version"] = k_FormatVersion;
        nlohmann::json environment = nlohmann::json::object();
        environment["skybox"] = "";
        environment["gravityDirection"] = nlohmann::json::array({0.0f, -1.0f, 0.0f});
        scene["environment"] = std::move(environment);
        scene["objects"] = nlohmann::json::array();
        scene["nextObjectId"] = 1u;
        return scene;
    }

    const nlohmann::json& SceneJsonObjects(const nlohmann::json& scene) noexcept
    {
        static const nlohmann::json k_Empty = nlohmann::json::array();
        if (!scene.is_object())
        {
            return k_Empty;
        }
        const nlohmann::json::const_iterator it = scene.find("objects");
        if (it == scene.end() || !it->is_array())
        {
            return k_Empty;
        }
        return *it;
    }

    nlohmann::json& SceneJsonObjects(nlohmann::json& scene)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        nlohmann::json& objects = scene["objects"];
        if (!objects.is_array())
        {
            objects = nlohmann::json::array();
        }
        return objects;
    }

    std::uint32_t SceneJsonNextObjectId(const nlohmann::json& scene) noexcept
    {
        return FieldUnsigned(scene, "nextObjectId", 1);
    }

    void SetSceneJsonNextObjectId(nlohmann::json& scene, std::uint32_t nextObjectId)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        scene["nextObjectId"] = nextObjectId;
    }

    std::string_view SceneJsonSkybox(const nlohmann::json& scene) noexcept
    {
        if (!scene.is_object())
        {
            return {};
        }
        const nlohmann::json::const_iterator environmentIt = scene.find("environment");
        if (environmentIt == scene.end() || !environmentIt->is_object())
        {
            return {};
        }
        const nlohmann::json::const_iterator skyboxIt = environmentIt->find("skybox");
        if (skyboxIt == environmentIt->end() || !skyboxIt->is_string())
        {
            return {};
        }
        return skyboxIt->get_ref<const std::string&>();
    }

    void SetSceneJsonSkybox(nlohmann::json& scene, std::string_view path)
    {
        EnsureEnvironment(scene)["skybox"] = std::string{path};
    }

    NS::Vector3 NormalizeGravityDirection(const NS::Vector3& direction) noexcept
    {
        const float lengthSquared = direction.LengthSquared();
        if (!NS::IsFinite(direction) || !std::isfinite(lengthSquared) ||
            !(lengthSquared > NS::k_Epsilon * NS::k_Epsilon))
        {
            return NS::Vector3{0.0f, -1.0f, 0.0f};
        }
        return direction / std::sqrt(lengthSquared);
    }

    NS::Vector3 SceneJsonGravityDirection(const nlohmann::json& scene) noexcept
    {
        if (!scene.is_object())
        {
            return NS::Vector3{0.0f, -1.0f, 0.0f};
        }
        const nlohmann::json::const_iterator environmentIt = scene.find("environment");
        if (environmentIt == scene.end() || !environmentIt->is_object())
        {
            return NS::Vector3{0.0f, -1.0f, 0.0f};
        }
        return NormalizeGravityDirection(
            FieldVector3(*environmentIt, "gravityDirection", NS::Vector3{0.0f, -1.0f, 0.0f}));
    }

    void SetSceneJsonGravityDirection(nlohmann::json& scene, const NS::Vector3& direction)
    {
        const NS::Vector3 normalized = NormalizeGravityDirection(direction);
        EnsureEnvironment(scene)["gravityDirection"] =
            nlohmann::json::array({normalized.x, normalized.y, normalized.z});
    }

    std::size_t FindObjectIndexById(const nlohmann::json& scene, std::uint32_t id) noexcept
    {
        if (id == k_NoObjectId)
        {
            return k_NoObjectIndex;
        }
        const nlohmann::json& objects = SceneJsonObjects(scene);
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (ObjectJsonId(objects[i]) == id)
            {
                return i;
            }
        }
        return k_NoObjectIndex;
    }

    void EnsureUniqueObjectIds(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);
        std::uint32_t nextObjectId = std::max(SceneJsonNextObjectId(scene), 1u);
        for (const nlohmann::json& object : objects)
        {
            nextObjectId = std::max(nextObjectId, ObjectJsonId(object) + 1);
        }
        std::unordered_set<std::uint32_t> seen;
        for (nlohmann::json& object : objects)
        {
            const std::uint32_t id = ObjectJsonId(object);
            if (id == 0 || !seen.insert(id).second)
            {
                const std::uint32_t fresh = nextObjectId++;
                SetObjectJsonId(object, fresh);
                seen.insert(fresh);
            }
        }
        SetSceneJsonNextObjectId(scene, nextObjectId);
        EnsureUniqueObjectNames(scene);
    }

    void EnsureUniqueObjectNames(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);
        std::unordered_set<std::string> used;
        std::vector<nlohmann::json*> pending;
        for (nlohmann::json& object : objects)
        {
            const std::string name{ObjectJsonName(object)};
            if (name.empty() || !used.insert(name).second)
            {
                pending.push_back(&object);
            }
        }
        for (nlohmann::json* object : pending)
        {
            const std::string name = MakeUniqueObjectName(ObjectJsonName(*object), used);
            SetObjectJsonName(*object, name);
            used.insert(name);
        }
    }

    std::size_t PruneDanglingObjectRefs(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);
        std::unordered_set<std::uint32_t> ids;
        for (const nlohmann::json& object : objects)
        {
            ids.insert(ObjectJsonId(object));
        }
        std::size_t pruned = 0;
        for (nlohmann::json& object : objects)
        {
            ForEachRefValue(object, [&ids, &pruned](nlohmann::json& value) {
                nlohmann::json& ref = value["ref"];
                if (!ref.is_number_unsigned())
                {
                    return;
                }
                const std::uint32_t id = ref.get<std::uint32_t>();
                if (id == 0 || ids.contains(id))
                {
                    return;
                }
                ref = 0u;
                ++pruned;
            });
        }
        return pruned;
    }

    std::size_t PruneInvalidParents(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);
        std::unordered_map<std::uint32_t, std::size_t> indexById;
        indexById.reserve(objects.size());
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            indexById.emplace(ObjectJsonId(objects[i]), i);
        }

        std::size_t prunedCount = 0;
        for (nlohmann::json& object : objects)
        {
            const std::uint32_t objectId = ObjectJsonId(object);
            const std::uint32_t parentId = ObjectJsonParent(object);
            if (parentId == k_NoObjectId)
            {
                continue;
            }

            bool valid = parentId != objectId && indexById.contains(parentId);
            // 祖先を辿って自分へ戻れば循環。その場で root へ落とすので、輪の残りは正当な親子として通る
            std::uint32_t ancestor = parentId;
            for (std::size_t step = 0; valid && step < objects.size(); ++step)
            {
                const std::unordered_map<std::uint32_t, std::size_t>::iterator it = indexById.find(ancestor);
                if (it == indexById.end())
                {
                    valid = false;
                    break;
                }
                ancestor = ObjectJsonParent(objects[it->second]);
                if (ancestor == k_NoObjectId)
                {
                    break;
                }
                if (ancestor == objectId)
                {
                    valid = false;
                }
            }

            if (!valid)
            {
                SetObjectJsonParent(object, k_NoObjectId);
                ++prunedCount;
            }
        }
        return prunedCount;
    }

    std::string SerializeSceneToJson(const nlohmann::json& scene)
    {
        nlohmann::json out = MakeSceneJson();
        SetSceneJsonSkybox(out, SceneJsonSkybox(scene));
        SetSceneJsonGravityDirection(out, SceneJsonGravityDirection(scene));
        SetSceneJsonNextObjectId(out, SceneJsonNextObjectId(scene));
        out["objects"] = SceneJsonObjects(scene);
        WriteRefsByName(out);

        // 不正 UTF-8 は replace で握り、dump が例外を投げないようにして noexcept 経路を保つ
        return out.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool DeserializeSceneFromJson(nlohmann::json& outScene, std::string_view jsonText)
    {
        outScene = MakeSceneJson();

        const nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
        if (root.is_discarded())
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: JSON parse に失敗");
            return false;
        }
        if (!root.is_object())
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: ルートが object でない");
            return false;
        }

        // 小数の version が切り捨てで一致に化けないよう、version の形式検査だけは整数のみ受ける
        const nlohmann::json::const_iterator versionIt = root.find("version");
        if (versionIt == root.end() || !versionIt->is_number_integer() || versionIt->get<int>() != k_FormatVersion)
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: version 欄が整数の {} と一致しない", k_FormatVersion);
            return false;
        }

        const nlohmann::json& objects = SceneJsonObjects(root);
        if (objects.size() > k_MaxObjectCount)
        {
            NS_LOG_ERROR(
                Scene, "DeserializeSceneFromJson: object 数 {} が上限 {} を超過", objects.size(), k_MaxObjectCount);
            return false;
        }
        nlohmann::json& outObjects = SceneJsonObjects(outScene);
        for (const nlohmann::json& objectJson : objects)
        {
            outObjects.push_back(NormalizeObject(objectJson));
        }

        // environment 欄は skybox だけを所有する。旧形式の照明の欄は DirectionalLight へ移ったので読み飛ばす
        SetSceneJsonSkybox(outScene, SceneJsonSkybox(root));
        SetSceneJsonGravityDirection(outScene, SceneJsonGravityDirection(root));
        SetSceneJsonNextObjectId(outScene, SceneJsonNextObjectId(root));

        // 手編集ファイルは id 未割当・重複があり得る。読込直後に必ず一意化し、以降の経路は id を信頼できる
        EnsureUniqueObjectIds(outScene);
        ReadRefsByName(outScene);

        // 手編集や参照先削除で宙に浮いた参照は入口で未設定へ戻す。実行時は id 照合の失敗を考えずに済む
        const std::size_t prunedRefs = PruneDanglingObjectRefs(outScene);
        if (prunedRefs > 0)
        {
            NS_LOG_WARN(Scene, "存在しない object を指す参照を {} 件未設定に戻した", prunedRefs);
        }
        // 循環した親を残すと world 変換の再帰が止まらないので入口で断つ
        const std::size_t prunedParents = PruneInvalidParents(outScene);
        if (prunedParents > 0)
        {
            NS_LOG_WARN(Scene, "辿れない親を持つ object を {} 件 root に戻した", prunedParents);
        }
        return true;
    }

    bool SaveSceneToJsonFile(const nlohmann::json& scene, std::string_view path) noexcept
    {
        // 保存前の上限ガード
        const std::size_t objectCount = SceneJsonObjects(scene).size();
        if (objectCount > k_MaxObjectCount)
        {
            NS_LOG_ERROR(Scene, "SaveSceneToJsonFile: object 数が上限超過 ({} > {})", objectCount, k_MaxObjectCount);
            return false;
        }
        const std::string text = SerializeSceneToJson(scene);
        if (text.size() > k_MaxSceneFileBytes)
        {
            NS_LOG_ERROR(Scene,
                         "SaveSceneToJsonFile: 出力 file が上限 ({} byte) を超過: {} byte",
                         k_MaxSceneFileBytes,
                         text.size());
            return false;
        }

        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        return ::NS::OS::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size()));
    }

    bool LoadSceneFromJsonFile(nlohmann::json& outScene, std::string_view path) noexcept
    {
        outScene = MakeSceneJson();

        std::optional<std::string> textOpt = ::NS::OS::FileSystem::ReadAllText(path);
        if (!textOpt.has_value())
        {
            return false;
        }

        if (textOpt->size() > k_MaxSceneFileBytes)
        {
            NS_LOG_ERROR(Scene,
                         "LoadSceneFromJsonFile: file が上限 ({} byte) を超えるので reject: {}",
                         k_MaxSceneFileBytes,
                         path);
            return false;
        }

        return DeserializeSceneFromJson(outScene, *textOpt);
    }
} // namespace NS::Obj
