#include "Framework/Scene/SceneJson.h"

#include "Framework/Core/Assert.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Scene/SceneData.h"

#include <cstddef>
#include <span>

namespace NS::Scene
{
    namespace
    {
        /// 保存形式のバージョン。 形式を変えたら上げ、 読込は一致のみ受け付ける
        constexpr int kFormatVersion = 1;

        /// 読込時の上限。 巨大 size / 要素数による memory exhaustion を防ぐ
        constexpr std::size_t kMaxSceneFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t kMaxObjectCount = 100'000u;
        constexpr std::size_t kMaxMaterialPaths = 4'096u;
        constexpr std::size_t kMaxMaterialPathLength = 1'024u;

        nlohmann::json Vec3Json(float x, float y, float z)
        {
            return nlohmann::json{x, y, z};
        }

        nlohmann::json Vec4Json(float x, float y, float z, float w)
        {
            return nlohmann::json{x, y, z, w};
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

        /// parent[key] が長さ 4 の数値配列なら x/y/z/w へ書き込む。 不在 / 型不一致は据え置きで前方互換を保つ
        void ReadVec4(const nlohmann::json& parent, const char* key, float& x, float& y, float& z, float& w)
        {
            const auto it = parent.find(key);
            if (it == parent.end() || !it->is_array() || it->size() < 4u)
                return;
            if (!(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number() || !(*it)[3].is_number())
                return;
            x = (*it)[0].get<float>();
            y = (*it)[1].get<float>();
            z = (*it)[2].get<float>();
            w = (*it)[3].get<float>();
        }

        /// parent[key] が数値なら int で返す。 不在 / 型不一致は fallback。 手編集の 1.0 形式も拾う
        int ReadInt(const nlohmann::json& parent, const char* key, int fallback)
        {
            const auto it = parent.find(key);
            if (it == parent.end() || !it->is_number())
                return fallback;
            return it->get<int>();
        }

        /// FieldValue の variant を JSON 値へ。 float と int は JSON の数値種別で区別され load
        /// 時に変種が復元される
        nlohmann::json FieldValueToJson(const FieldValue& field)
        {
            static_assert(std::variant_size_v<decltype(FieldValue::value)> == 6u,
                          "FieldValue の変種が変わった。 この switch と JsonToFieldValue の推論を追従させる");
            switch (field.value.index())
            {
            case 0:
                return std::get<float>(field.value);
            case 1:
                return std::get<int>(field.value);
            case 2:
                return std::get<bool>(field.value);
            case 3:
            {
                const auto& vector3 = std::get<NS::Math::Vector3>(field.value);
                return Vec3Json(vector3.x, vector3.y, vector3.z);
            }
            case 4:
                return std::get<std::string>(field.value);
            case 5:
            {
                // 素の数値だと load 時の推論で int に化けるため {"ref": id} の単キー object で書く
                nlohmann::json ref;
                ref["ref"] = std::get<ObjectRef>(field.value).id;
                return ref;
            }
            default:
                // 変種の増加は上の static_assert が拘束するため、 ここに来るのは値を失った variant だけ
                NS_ASSERT(::NS::Core::LogCat::Scene,
                          false,
                          "FieldValue \"{}\" が有効な変種を持っていない (index {})",
                          field.name,
                          field.value.index());
                return nlohmann::json{};
            }
        }

        nlohmann::json SerializeComponentData(const ComponentData& component)
        {
            nlohmann::json out;
            out["type"] = component.typeName;
            out["fields"] = ComponentFieldsToJson(component);
            return out;
        }

        ComponentData DeserializeComponentData(const nlohmann::json& json)
        {
            ComponentData component;
            if (!json.is_object())
                return component;
            const auto typeIt = json.find("type");
            if (typeIt != json.end() && typeIt->is_string())
                component.typeName = typeIt->get<std::string>();
            const auto fieldsIt = json.find("fields");
            if (fieldsIt != json.end() && fieldsIt->is_object())
            {
                for (const auto& [name, value] : fieldsIt->items())
                {
                    FieldValue parsed;
                    if (JsonToFieldValue(name, value, parsed))
                        component.fields.push_back(std::move(parsed));
                }
            }
            return component;
        }

        nlohmann::json SerializeObject(const ObjectData& object)
        {
            nlohmann::json components = nlohmann::json::array();
            for (const auto& component : object.components)
                components.push_back(SerializeComponentData(component));

            nlohmann::json out;
            out["id"] = object.objectId;
            out["position"] = Vec3Json(object.positionX, object.positionY, object.positionZ);
            out["rotation"] = Vec4Json(object.rotationX, object.rotationY, object.rotationZ, object.rotationW);
            out["scale"] = Vec3Json(object.scaleX, object.scaleY, object.scaleZ);
            out["material"] = static_cast<int>(object.materialIndex);
            out["components"] = std::move(components);
            return out;
        }

        ObjectData DeserializeObject(const nlohmann::json& json)
        {
            ObjectData object{};
            if (!json.is_object())
                return object;

            ReadVec3(json, "position", object.positionX, object.positionY, object.positionZ);
            ReadVec4(json, "rotation", object.rotationX, object.rotationY, object.rotationZ, object.rotationW);
            ReadVec3(json, "scale", object.scaleX, object.scaleY, object.scaleZ);
            object.objectId = static_cast<std::uint32_t>(ReadInt(json, "id", 0));
            object.materialIndex = static_cast<std::int16_t>(ReadInt(json, "material", object.materialIndex));

            const auto componentsIt = json.find("components");
            if (componentsIt != json.end() && componentsIt->is_array())
            {
                object.components.reserve(componentsIt->size());
                for (const auto& componentJson : *componentsIt)
                    object.components.push_back(DeserializeComponentData(componentJson));
            }
            return object;
        }
    } // namespace

    nlohmann::json ComponentFieldsToJson(const ComponentData& component)
    {
        nlohmann::json fields = nlohmann::json::object();
        for (const auto& field : component.fields)
            fields[field.name] = FieldValueToJson(field);
        return fields;
    }

    bool JsonToFieldValue(const std::string& name, const nlohmann::json& value, FieldValue& out)
    {
        if (value.is_boolean())
        {
            out = FieldValue{name, value.get<bool>()};
            return true;
        }
        if (value.is_number_float())
        {
            out = FieldValue{name, value.get<float>()};
            return true;
        }
        if (value.is_number_integer() || value.is_number_unsigned())
        {
            out = FieldValue{name, value.get<int>()};
            return true;
        }
        if (value.is_string())
        {
            out = FieldValue{name, value.get<std::string>()};
            return true;
        }
        if (value.is_array() && value.size() == 3u && value[0].is_number() && value[1].is_number() &&
            value[2].is_number())
        {
            out = FieldValue{name,
                             NS::Math::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()}};
            return true;
        }
        if (value.is_object())
        {
            const auto refIt = value.find("ref");
            // 負数は id として不正なので unsigned のみ受ける。 壊れた ref は積まずに前方互換へ倒す
            if (refIt != value.end() && refIt->is_number_unsigned())
            {
                out = FieldValue{name, ObjectRef{refIt->get<std::uint32_t>()}};
                return true;
            }
            return false;
        }
        return false;
    }

    std::string SerializeSceneToJson(const SceneData& scene)
    {
        nlohmann::json root;
        root["version"] = kFormatVersion;

        nlohmann::json environment;
        environment["lightDirection"] = Vec3Json(
            scene.environment.lightDirection.x, scene.environment.lightDirection.y, scene.environment.lightDirection.z);
        environment["lightColor"] =
            Vec3Json(scene.environment.lightColor.x, scene.environment.lightColor.y, scene.environment.lightColor.z);
        environment["ambientColor"] = Vec3Json(
            scene.environment.ambientColor.x, scene.environment.ambientColor.y, scene.environment.ambientColor.z);
        environment["skybox"] = scene.environment.skyboxCubemapPath;
        root["environment"] = std::move(environment);

        nlohmann::json objects = nlohmann::json::array();
        for (const auto& object : scene.objects)
            objects.push_back(SerializeObject(object));
        root["objects"] = std::move(objects);
        root["nextObjectId"] = scene.nextObjectId;

        nlohmann::json materials = nlohmann::json::array();
        for (const auto& materialPath : scene.materialPaths)
            materials.push_back(materialPath);
        root["materials"] = std::move(materials);

        // 不正 UTF-8 は replace で握り、 dump が例外を投げないようにして noexcept 経路を保つ
        return root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool DeserializeSceneFromJson(SceneData& outScene, std::string_view jsonText)
    {
        outScene = SceneData{};

        const nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
        if (root.is_discarded())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Scene, "DeserializeSceneFromJson: JSON parse に失敗");
            return false;
        }
        if (!root.is_object())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Scene, "DeserializeSceneFromJson: ルートが object でない");
            return false;
        }

        // 小数の version が切り捨てで一致に化けないよう、 形式契約の門だけは整数のみ受ける
        const auto versionIt = root.find("version");
        if (versionIt == root.end() || !versionIt->is_number_integer() || versionIt->get<int>() != kFormatVersion)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                         "DeserializeSceneFromJson: version 欄が整数の {} と一致しない",
                         kFormatVersion);
            return false;
        }

        const auto objectsIt = root.find("objects");
        if (objectsIt != root.end() && objectsIt->is_array())
        {
            if (objectsIt->size() > kMaxObjectCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                             "DeserializeSceneFromJson: object 数 {} が上限 {} を超過",
                             objectsIt->size(),
                             kMaxObjectCount);
                outScene = SceneData{};
                return false;
            }
            outScene.objects.reserve(objectsIt->size());
            for (const auto& objectJson : *objectsIt)
                outScene.objects.push_back(DeserializeObject(objectJson));
        }

        const auto materialsIt = root.find("materials");
        if (materialsIt != root.end() && materialsIt->is_array())
        {
            if (materialsIt->size() > kMaxMaterialPaths)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                             "DeserializeSceneFromJson: material path 数 {} が上限 {} を超過",
                             materialsIt->size(),
                             kMaxMaterialPaths);
                outScene = SceneData{};
                return false;
            }
            for (const auto& materialJson : *materialsIt)
            {
                if (!materialJson.is_string())
                    continue;
                std::string materialPath = materialJson.get<std::string>();
                if (materialPath.size() > kMaxMaterialPathLength)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                                 "DeserializeSceneFromJson: material path が長すぎる ({} > {} byte)",
                                 materialPath.size(),
                                 kMaxMaterialPathLength);
                    outScene = SceneData{};
                    return false;
                }
                outScene.materialPaths.push_back(std::move(materialPath));
            }
        }

        // environment 欄がシーンの見た目を所有する。 中立の既定値の上に読めたキーだけ部分適用する
        const auto environmentIt = root.find("environment");
        if (environmentIt != root.end() && environmentIt->is_object())
        {
            ReadVec3(*environmentIt,
                     "lightDirection",
                     outScene.environment.lightDirection.x,
                     outScene.environment.lightDirection.y,
                     outScene.environment.lightDirection.z);
            ReadVec3(*environmentIt,
                     "lightColor",
                     outScene.environment.lightColor.x,
                     outScene.environment.lightColor.y,
                     outScene.environment.lightColor.z);
            ReadVec3(*environmentIt,
                     "ambientColor",
                     outScene.environment.ambientColor.x,
                     outScene.environment.ambientColor.y,
                     outScene.environment.ambientColor.z);
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
            NS_LOG_WARN(::NS::Core::LogCat::Scene, "存在しない object を指す参照を {} 件未設定に戻した", prunedRefs);

        return true;
    }

    bool SaveSceneToJsonFile(const SceneData& scene, const std::filesystem::path& path) noexcept
    {
        if (scene.objects.size() > kMaxObjectCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                         "SaveSceneToJsonFile: object 数が上限超過 ({} > {})",
                         scene.objects.size(),
                         kMaxObjectCount);
            return false;
        }
        if (scene.materialPaths.size() > kMaxMaterialPaths)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                         "SaveSceneToJsonFile: material path 数が上限超過 ({} > {})",
                         scene.materialPaths.size(),
                         kMaxMaterialPaths);
            return false;
        }
        // load 側が同じ閾値で拒否するため、 上限超過パスは保存段で弾いて往復不能を防ぐ
        for (const auto& materialPath : scene.materialPaths)
        {
            if (materialPath.size() > kMaxMaterialPathLength)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                             "SaveSceneToJsonFile: material path が長すぎる ({} > {} byte)",
                             materialPath.size(),
                             kMaxMaterialPathLength);
                return false;
            }
        }
        // json の構築 / dump は bad_alloc を投げ得る。 noexcept 契約を守るため捕捉して false に変換する
        try
        {
            const std::string text = SerializeSceneToJson(scene);
            if (text.size() > kMaxSceneFileBytes)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                             "SaveSceneToJsonFile: 出力 file が上限 ({} byte) を超過: {} byte",
                             kMaxSceneFileBytes,
                             text.size());
                return false;
            }

            const auto* raw = reinterpret_cast<const std::byte*>(text.data());
            return ::NS::Core::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size()));
        }
        catch (...)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Scene, "SaveSceneToJsonFile: 直列化中に例外を捕捉");
            return false;
        }
    }

    bool LoadSceneFromJsonFile(SceneData& outScene, const std::filesystem::path& path) noexcept
    {
        outScene = SceneData{};

        // 全文読み・parse・SceneData 構築のいずれも bad_alloc を投げ得る。 noexcept 契約を守るため捕捉する
        try
        {
            auto textOpt = ::NS::Core::FileSystem::ReadAllText(path);
            if (!textOpt.has_value())
                return false;

            if (textOpt->size() > kMaxSceneFileBytes)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Scene,
                             "LoadSceneFromJsonFile: file が上限 ({} byte) を超えるので reject: {}",
                             kMaxSceneFileBytes,
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
            NS_LOG_ERROR(::NS::Core::LogCat::Scene, "LoadSceneFromJsonFile: 読込中に例外を捕捉");
            outScene = SceneData{};
            return false;
        }
        return true;
    }
} // namespace NS::Scene
