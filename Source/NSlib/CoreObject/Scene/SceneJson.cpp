#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/Windows/Filesystem.h"

#include <algorithm>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NS
{
    namespace
    {
        // 保存形式を変えたら上げる。読み込みは一致する版だけを受け付ける
        constexpr int k_FormatVersion = 8;

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
            WriteObjectHeader(object, ReadObjectHeader(json));
            nlohmann::json& parts = ObjectJsonClassObjs(object);
            const nlohmann::json& source = ObjectJsonClassObjs(json);
            for (nlohmann::json::const_iterator part = source.begin(); part != source.end(); ++part)
            {
                if (part.value().is_object())
                {
                    parts[part.key()] = part.value();
                }
            }
            return object;
        }

        // ファイルの参照は相手の名前で書く。空と重複した名前は相手が 1 つに決まらないので id のまま残す
        void WriteRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonObjects(scene);
            std::unordered_map<std::string, int> counts;
            std::unordered_map<uint32, std::string> names;
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
                ForEachRefValue(object, [&names](nlohmann::json& ref) {
                    if (!ref.is_number_unsigned())
                    {
                        return;
                    }
                    const std::unordered_map<uint32, std::string>::const_iterator found =
                        names.find(ref.get<uint32>());
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
            std::unordered_map<std::string, uint32> ids;
            for (const nlohmann::json& object : objects)
            {
                ids.emplace(std::string{ObjectJsonName(object)}, ObjectJsonId(object));
            }
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&ids](nlohmann::json& ref) {
                    if (!ref.is_string())
                    {
                        return;
                    }
                    const std::unordered_map<std::string, uint32>::const_iterator found =
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

    } // namespace

    nlohmann::json MakeSceneJson()
    {
        nlohmann::json scene = nlohmann::json::object();
        scene["version"] = k_FormatVersion;
        WriteSceneEnvironment(scene, SceneEnvironment{});
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

    uint32 SceneJsonNextObjectId(const nlohmann::json& scene) noexcept
    {
        return FieldUnsigned(scene, "nextObjectId", 1);
    }

    void SetSceneJsonNextObjectId(nlohmann::json& scene, uint32 nextObjectId)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        scene["nextObjectId"] = nextObjectId;
    }

    SceneEnvironment ReadSceneEnvironment(const nlohmann::json& scene)
    {
        SceneEnvironment environment{};
        if (!scene.is_object())
        {
            return environment;
        }
        const nlohmann::json::const_iterator found = scene.find("environment");
        if (found == scene.end() || !found->is_object())
        {
            return environment;
        }
        environment.skybox = FieldString(*found, "skybox", "");
        return environment;
    }

    void WriteSceneEnvironment(nlohmann::json& scene, const SceneEnvironment& environment)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        nlohmann::json out = nlohmann::json::object();
        out["skybox"] = environment.skybox;
        scene["environment"] = std::move(out);
    }

    std::string SceneJsonSkybox(const nlohmann::json& scene)
    {
        return ReadSceneEnvironment(scene).skybox;
    }

    uint32 NextFreeObjectId(const nlohmann::json& scene) noexcept
    {
        uint32 next = std::max(SceneJsonNextObjectId(scene), 1u);
        for (const nlohmann::json& object : SceneJsonObjects(scene))
        {
            next = std::max(next, ObjectJsonId(object) + 1);
        }
        return next;
    }

    std::size_t FindObjectIndexById(const nlohmann::json& scene, uint32 id) noexcept
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

    namespace
    {
        // ファイルの参照は名前で書くので、名前が 1 つに決まることを保存と読込が当てにする
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
                const std::string name = MakeUniqueActorName(ObjectJsonName(*object), used);
                SetObjectJsonName(*object, name);
                used.insert(name);
            }
        }

        // 存在しない object を指す参照を 0 へ戻した件数を返す。0 は元から未設定なので数えない
        std::size_t PruneDanglingObjectRefs(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonObjects(scene);
            std::unordered_set<uint32> ids;
            for (const nlohmann::json& object : objects)
            {
                ids.insert(ObjectJsonId(object));
            }
            std::size_t pruned = 0;
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&ids, &pruned](nlohmann::json& ref) {
                    if (!ref.is_number_unsigned())
                    {
                        return;
                    }
                    const uint32 id = ref.get<uint32>();
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

    } // namespace

    void EnsureUniqueObjectIds(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);
        uint32 nextObjectId = NextFreeObjectId(scene);
        std::unordered_set<uint32> seen;
        for (nlohmann::json& object : objects)
        {
            const uint32 id = ObjectJsonId(object);
            if (id == 0 || !seen.insert(id).second)
            {
                const uint32 fresh = nextObjectId++;
                SetObjectJsonId(object, fresh);
                seen.insert(fresh);
            }
        }
        SetSceneJsonNextObjectId(scene, nextObjectId);
        EnsureUniqueObjectNames(scene);
    }

    std::string MakeUniqueActorName(std::string_view base, const std::unordered_set<std::string>& used)
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
        for (uint32 n = 1;; ++n)
        {
            std::string candidate = name + "_" + std::to_string(n);
            if (!used.contains(candidate))
            {
                return candidate;
            }
        }
    }

    std::string SerializeSceneToJson(const nlohmann::json& scene)
    {
        nlohmann::json out = MakeSceneJson();
        WriteSceneEnvironment(out, ReadSceneEnvironment(scene));
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

        // environment の知らない欄は読み飛ばす。旧形式の照明の欄は DirectionalLight へ移った
        WriteSceneEnvironment(outScene, ReadSceneEnvironment(root));
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
        return OS::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size()));
    }

    bool LoadSceneFromJsonFile(nlohmann::json& outScene, std::string_view path) noexcept
    {
        outScene = MakeSceneJson();

        std::optional<std::string> textOpt = OS::FileSystem::ReadAllText(path);
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
} // namespace NS
