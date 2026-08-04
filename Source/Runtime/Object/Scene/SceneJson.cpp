#include "Runtime/Object/Scene/SceneJson.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <cstddef>
#include <span>

namespace NS::Object
{
    namespace
    {
        /// 保存形式のバージョン。 形式を変えたら上げ、 読込は一致のみ受け付ける
        constexpr int k_FormatVersion = 2;

        /// 読込時の上限。 巨大 size / 要素数による メモリ枯渇を防ぐ
        constexpr std::size_t k_MaxSceneFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t k_MaxObjectCount = 100'000u;

        nlohmann::json Vec3Json(float x, float y, float z)
        {
            return nlohmann::json{x, y, z};
        }

        /// parent[key] が長さ 3 の数値配列なら x/y/z へ書き込む。 不在 / 型不一致は据え置きで前方互換を保つ
        void ReadVec3(const nlohmann::json& parent, const char* key, float& x, float& y, float& z)
        {
            const auto it = parent.find(key);
            if (it == parent.end() || !it->is_array() || it->size() < 3u)
                return;
            if (!(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number())
                return;
            x = (*it)[0].get<float>();
            y = (*it)[1].get<float>();
            z = (*it)[2].get<float>();
        }

        /// parent[key] が数値なら int で返す。 不在 / 型不一致は fallback。 手編集の 1.0 形式も拾う
        int ReadInt(const nlohmann::json& parent, const char* key, int fallback)
        {
            const auto it = parent.find(key);
            if (it == parent.end() || !it->is_number())
                return fallback;
            return it->get<int>();
        }

        /// transform エントリから内部の回転控えを落とす。 ファイルは Euler の "Rotation (deg)" だけ残す
        void StripRotationQuatField(nlohmann::json& components)
        {
            if (!components.is_array())
                return;
            for (nlohmann::json& entry : components)
            {
                if (ComponentEntryType(entry) != k_TransformTypeName)
                    continue;
                const auto fieldsIt = entry.find("fields");
                if (fieldsIt != entry.end() && fieldsIt->is_object())
                    fieldsIt->erase(std::string(k_RotationQuatFieldName));
            }
        }

        nlohmann::json SerializeObject(const ObjectData& object)
        {
            nlohmann::json out;
            out["id"] = object.objectId;
            // GameObject のクラス名。 素の GameObject は書かず、 読込側は不在を空として扱う
            if (!object.className.empty())
                out["class"] = object.className;
            // 表示名は付いている物だけ書き、 未設定は型からの導出に任せる
            if (!object.name.empty())
                out["name"] = object.name;
            // root は書かず、 読込側は不在を 0 として扱う
            if (object.parentId != k_NoObjectId)
                out["parent"] = object.parentId;
            // 既定値は書かない。 order 0 と active true は不在で表す
            if (object.order != 0)
                out["order"] = object.order;
            if (!object.active)
                out["active"] = false;
            // components はメモリ上も保存形式と同じ {type, fields} の JSON 配列なのでそのまま書く
            out["components"] = object.components;
            // メモリ上は厳密なクォータニオンを控えるが、 ファイルは version 2 の Euler 表現だけにして byte 安定を保つ
            StripRotationQuatField(out["components"]);
            return out;
        }

        ObjectData DeserializeObject(const nlohmann::json& json)
        {
            ObjectData object{};
            if (!json.is_object())
                return object;

            object.objectId = static_cast<std::uint32_t>(ReadInt(json, "id", 0));
            const auto classIt = json.find("class");
            if (classIt != json.end() && classIt->is_string())
                object.className = classIt->get<std::string>();
            const auto nameIt = json.find("name");
            if (nameIt != json.end() && nameIt->is_string())
                object.name = nameIt->get<std::string>();
            object.parentId = static_cast<std::uint32_t>(ReadInt(json, "parent", 0));
            object.order = static_cast<std::uint32_t>(ReadInt(json, "order", 0));
            // 欄が無い古いファイルは有効として読む
            const auto activeIt = json.find("active");
            if (activeIt != json.end() && activeIt->is_boolean())
                object.active = activeIt->get<bool>();

            const auto componentsIt = json.find("components");
            if (componentsIt != json.end() && componentsIt->is_array())
            {
                // {type, id, fields} の骨格だけ整えて受け取る。 未知キーは捨て、 fields の中身は素通し
                for (const auto& componentJson : *componentsIt)
                {
                    if (!componentJson.is_object())
                        continue;
                    nlohmann::json fields = nlohmann::json::object();
                    const auto fieldsIt = componentJson.find("fields");
                    if (fieldsIt != componentJson.end() && fieldsIt->is_object())
                        fields = *fieldsIt;
                    nlohmann::json entry =
                        MakeComponentEntry(componentJson.value("type", std::string{}), std::move(fields));
                    // id を落とすと読むたびに振り直しになり、 名指ししている参照が外れる
                    SetComponentEntryId(entry, ComponentEntryId(componentJson));
                    object.components.push_back(std::move(entry));
                }
            }
            // 手編集で transform エントリを欠くファイルにも 1 つ保証し、以降の transform 読取を成立させる
            EnsureTransformComponent(object);
            return object;
        }
    } // namespace

    std::string SerializeSceneToJson(const SceneData& scene)
    {
        nlohmann::json root;
        root["version"] = k_FormatVersion;

        nlohmann::json environment;
        environment["skybox"] = scene.environment.skyboxCubemapPath;
        root["environment"] = std::move(environment);

        nlohmann::json objects = nlohmann::json::array();
        for (const auto& object : scene.objects)
            objects.push_back(SerializeObject(object));
        root["objects"] = std::move(objects);
        root["nextObjectId"] = scene.nextObjectId;

        // 不正 UTF-8 は replace で握り、 dump が例外を投げないようにして noexcept 経路を保つ
        return root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool DeserializeSceneFromJson(SceneData& outScene, std::string_view jsonText)
    {
        outScene = SceneData{};

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

        // 小数の version が切り捨てで一致に化けないよう、 version の形式検査だけは整数のみ受ける
        const auto versionIt = root.find("version");
        if (versionIt == root.end() || !versionIt->is_number_integer() || versionIt->get<int>() != k_FormatVersion)
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: version 欄が整数の {} と一致しない", k_FormatVersion);
            return false;
        }

        // objects を上限ガード付きで読む
        const auto objectsIt = root.find("objects");
        if (objectsIt != root.end() && objectsIt->is_array())
        {
            if (objectsIt->size() > k_MaxObjectCount)
            {
                NS_LOG_ERROR(Scene,
                             "DeserializeSceneFromJson: object 数 {} が上限 {} を超過",
                             objectsIt->size(),
                             k_MaxObjectCount);
                outScene = SceneData{};
                return false;
            }
            outScene.objects.reserve(objectsIt->size());
            for (const auto& objectJson : *objectsIt)
                outScene.objects.push_back(DeserializeObject(objectJson));
        }

        // environment 欄は skybox だけを所有する。 旧形式の lightDirection / lightColor / ambientColor は
        // 照明が DirectionalLightComponent へ移ったので、 キーが残っていても読み飛ばす
        const auto environmentIt = root.find("environment");
        if (environmentIt != root.end() && environmentIt->is_object())
        {
            const auto skyboxIt = environmentIt->find("skybox");
            if (skyboxIt != environmentIt->end() && skyboxIt->is_string())
                outScene.environment.skyboxCubemapPath = skyboxIt->get<std::string>();
        }

        outScene.nextObjectId = static_cast<std::uint32_t>(ReadInt(root, "nextObjectId", 1));
        // 手編集ファイルは id 未割当・重複があり得る。読込直後に必ず一意化し、以降の経路は id を信頼できる
        EnsureUniqueObjectIds(outScene);

        // 手編集や参照先削除で宙に浮いた参照は入口で未設定へ戻す。実行時は id 照合の失敗を考えずに済む
        const std::size_t prunedRefs = PruneDanglingObjectRefs(outScene);
        if (prunedRefs > 0)
            NS_LOG_WARN(Scene, "存在しない object を指す参照を {} 件未設定に戻した", prunedRefs);

        // 循環した親を残すと world 変換の再帰が止まらないので入口で断つ
        const std::size_t prunedParents = PruneInvalidParents(outScene);
        if (prunedParents > 0)
            NS_LOG_WARN(Scene, "辿れない親を持つ object を {} 件 root に戻した", prunedParents);

        return true;
    }

    bool SaveSceneToJsonFile(const SceneData& scene, const std::filesystem::path& path) noexcept
    {
        // 保存前の上限ガード
        if (scene.objects.size() > k_MaxObjectCount)
        {
            NS_LOG_ERROR(
                Scene, "SaveSceneToJsonFile: object 数が上限超過 ({} > {})", scene.objects.size(), k_MaxObjectCount);
            return false;
        }
        // json の構築 / dump は bad_alloc を投げ得る。 noexcept を守るため捕捉して false に変換する
        try
        {
            const std::string text = SerializeSceneToJson(scene);
            if (text.size() > k_MaxSceneFileBytes)
            {
                NS_LOG_ERROR(Scene,
                             "SaveSceneToJsonFile: 出力 file が上限 ({} byte) を超過: {} byte",
                             k_MaxSceneFileBytes,
                             text.size());
                return false;
            }

            const auto* raw = reinterpret_cast<const std::byte*>(text.data());
            return ::NS::Core::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size()));
        }
        catch (...)
        {
            NS_LOG_ERROR(Scene, "SaveSceneToJsonFile: 直列化中に例外を捕捉");
            return false;
        }
    }

    bool LoadSceneFromJsonFile(SceneData& outScene, const std::filesystem::path& path) noexcept
    {
        outScene = SceneData{};

        // 全文読み・parse・SceneData 構築のいずれも bad_alloc を投げ得る。 noexcept を守るため捕捉する
        try
        {
            auto textOpt = ::NS::Core::FileSystem::ReadAllText(path);
            if (!textOpt.has_value())
                return false;

            if (textOpt->size() > k_MaxSceneFileBytes)
            {
                NS_LOG_ERROR(Scene,
                             "LoadSceneFromJsonFile: file が上限 ({} byte) を超えるので reject: {}",
                             k_MaxSceneFileBytes,
                             path.string());
                return false;
            }

            if (!DeserializeSceneFromJson(outScene, *textOpt))
            {
                outScene = SceneData{};
                return false;
            }
        }
        catch (...)
        {
            NS_LOG_ERROR(Scene, "LoadSceneFromJsonFile: 読込中に例外を捕捉");
            outScene = SceneData{};
            return false;
        }
        return true;
    }
} // namespace NS::Object
