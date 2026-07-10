#include "Game/Level/LevelJson.h"

#include "Game/Level/LevelData.h"
#include "Game/Level/LevelIO.h" // LevelLoadReport の完全型

// json.hpp は /W4 で警告が出るため、 この翻訳単位でだけ警告を抑止して取り込む
#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Game::Level
{
    namespace
    {
        /// セーブフォーマットのバージョン。 binary 時代の major/minor を 1 整数へ置換した
        /// v2 で spawn をグリッドセル番号から capsule 中心の world 位置 + 向きへ変更した
        /// v3 で object へ永続 id、 root へ nextObjectId を追加した。 旧版は読込時に採番して移行する
        /// v4 で据え置きカメラを cameraVolumes の別リストから objects の配置物へ統合した
        /// v5 でプレイヤーを spawn 単一値から objects の実体へ統合した。 旧版は読込時に合成して移行する
        /// v6 で追従カメラを scene 直組みから objects の実体へ統合した。 旧版は読込時に合成して移行する
        /// v7 で環境をシーン所有の environment 欄へ統合し themeId を廃止した。 environment 欄が無い
        /// 旧ファイルは中立の既定値で読む
        /// v8 で gridAligned フラグを廃止した。 旧ファイルの flags キーは読み飛ばす
        /// v9 で blockTextureBaseSlice を廃止した。 旧ファイルの当該キーは読み飛ばす
        constexpr int kFormatVersion = 9;

        /// 読込時の上限。 巨大 size / 要素数による memory exhaustion を防ぐ。 binary 版から移植
        constexpr std::size_t kMaxLevelFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t kMaxObjectCount = 100'000u;
        constexpr std::size_t kMaxMaterialPaths = 4'096u;
        constexpr std::size_t kMaxMaterialPathLength = 1'024u;
        constexpr std::size_t kMaxLegacyCameraVolumeCount = 4'096u;

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

        /// FieldValue の variant を JSON 値へ。 float と int は JSON の数値種別で区別され load 時に変種が復元される
        nlohmann::json FieldValueToJson(const FieldValue& field)
        {
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
                ref["ref"] = std::get<NS::Scene::ObjectRef>(field.value).id;
                return ref;
            }
            default:
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

        nlohmann::json SerializeObject(const ObjectInstance& object)
        {
            nlohmann::json transform;
            transform["pos"] = Vec3Json(object.positionX, object.positionY, object.positionZ);
            transform["rot"] = Vec4Json(object.rotationX, object.rotationY, object.rotationZ, object.rotationW);
            transform["scale"] = Vec3Json(object.scaleX, object.scaleY, object.scaleZ);

            nlohmann::json collider;
            collider["shape"] = static_cast<int>(object.shapeCollider);
            collider["halfExtents"] =
                Vec3Json(object.colliderHalfExtentsX, object.colliderHalfExtentsY, object.colliderHalfExtentsZ);
            collider["offset"] = Vec3Json(object.colliderOffsetX, object.colliderOffsetY, object.colliderOffsetZ);
            collider["rotation"] = Vec4Json(
                object.colliderRotationX, object.colliderRotationY, object.colliderRotationZ, object.colliderRotationW);

            nlohmann::json components = nlohmann::json::array();
            for (const auto& component : object.components)
                components.push_back(SerializeComponentData(component));

            nlohmann::json out;
            out["id"] = object.objectId;
            out["transform"] = std::move(transform);
            out["materialIndex"] = static_cast<int>(object.materialIndex);
            out["reserved1"] = static_cast<int>(object.reserved1);
            out["collider"] = std::move(collider);
            out["components"] = std::move(components);
            return out;
        }

        ObjectInstance DeserializeObject(const nlohmann::json& json)
        {
            ObjectInstance object{};
            if (!json.is_object())
                return object;

            const auto transformIt = json.find("transform");
            if (transformIt != json.end() && transformIt->is_object())
            {
                ReadVec3(*transformIt, "pos", object.positionX, object.positionY, object.positionZ);
                ReadVec4(*transformIt, "rot", object.rotationX, object.rotationY, object.rotationZ, object.rotationW);
                ReadVec3(*transformIt, "scale", object.scaleX, object.scaleY, object.scaleZ);
            }

            object.objectId = static_cast<std::uint32_t>(ReadInt(json, "id", 0));
            object.materialIndex = static_cast<std::int16_t>(ReadInt(json, "materialIndex", object.materialIndex));
            object.reserved1 = static_cast<std::uint16_t>(ReadInt(json, "reserved1", object.reserved1));

            const auto colliderIt = json.find("collider");
            if (colliderIt != json.end() && colliderIt->is_object())
            {
                object.shapeCollider = static_cast<std::uint8_t>(ReadInt(*colliderIt, "shape", object.shapeCollider));
                ReadVec3(*colliderIt,
                         "halfExtents",
                         object.colliderHalfExtentsX,
                         object.colliderHalfExtentsY,
                         object.colliderHalfExtentsZ);
                ReadVec3(*colliderIt, "offset", object.colliderOffsetX, object.colliderOffsetY, object.colliderOffsetZ);
                ReadVec4(*colliderIt,
                         "rotation",
                         object.colliderRotationX,
                         object.colliderRotationY,
                         object.colliderRotationZ,
                         object.colliderRotationW);
            }

            const auto componentsIt = json.find("components");
            if (componentsIt != json.end() && componentsIt->is_array())
            {
                object.components.reserve(componentsIt->size());
                for (const auto& componentJson : *componentsIt)
                    object.components.push_back(DeserializeComponentData(componentJson));
            }
            return object;
        }

        /// v3 以前の cameraVolumes 1 件を PlacedVirtualCamera 持ちの配置物へ変換する。読込移行専用
        /// 視点位置は object の Transform、それ以外は component の反射フィールドに載せ替える
        ObjectInstance MakeCameraObjectFromLegacyVolume(const nlohmann::json& json)
        {
            ObjectInstance object{};
            object.materialIndex = -1;
            if (!json.is_object())
                return object;

            ReadVec3(json, "cameraPosition", object.positionX, object.positionY, object.positionZ);

            float lookTargetX = 0.0f;
            float lookTargetY = 0.0f;
            float lookTargetZ = 0.0f;
            ReadVec3(json, "lookTarget", lookTargetX, lookTargetY, lookTargetZ);
            float triggerCenterX = 0.0f;
            float triggerCenterY = 0.0f;
            float triggerCenterZ = 0.0f;
            ReadVec3(json, "triggerCenter", triggerCenterX, triggerCenterY, triggerCenterZ);
            float triggerExtentX = 1.0f;
            float triggerExtentY = 1.0f;
            float triggerExtentZ = 1.0f;
            ReadVec3(json, "triggerExtent", triggerExtentX, triggerExtentY, triggerExtentZ);

            ComponentData camera;
            camera.typeName = "PlacedVirtualCamera";
            camera.fields.push_back(
                FieldValue{"Look Target", NS::Math::Vector3{lookTargetX, lookTargetY, lookTargetZ}});
            camera.fields.push_back(
                FieldValue{"Trigger Center", NS::Math::Vector3{triggerCenterX, triggerCenterY, triggerCenterZ}});
            camera.fields.push_back(
                FieldValue{"Trigger Extent", NS::Math::Vector3{triggerExtentX, triggerExtentY, triggerExtentZ}});
            camera.fields.push_back(FieldValue{"Look At Player", ReadInt(json, "lookAtPlayer", 0) != 0});
            camera.fields.push_back(FieldValue{"Priority", ReadInt(json, "priority", 10)});
            object.components.push_back(std::move(camera));
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
                out = FieldValue{name, NS::Scene::ObjectRef{refIt->get<std::uint32_t>()}};
                return true;
            }
            return false;
        }
        return false;
    }

    std::string SerializeLevelToJson(const LevelData& level)
    {
        nlohmann::json root;
        root["formatVersion"] = kFormatVersion;

        nlohmann::json environment;
        environment["lightDirection"] = Vec3Json(
            level.environment.lightDirection.x, level.environment.lightDirection.y, level.environment.lightDirection.z);
        environment["lightColor"] =
            Vec3Json(level.environment.lightColor.x, level.environment.lightColor.y, level.environment.lightColor.z);
        environment["ambientColor"] = Vec3Json(
            level.environment.ambientColor.x, level.environment.ambientColor.y, level.environment.ambientColor.z);
        environment["skyboxCubemapPath"] = level.environment.skyboxCubemapPath;
        root["environment"] = std::move(environment);

        nlohmann::json objects = nlohmann::json::array();
        for (const auto& object : level.objects)
            objects.push_back(SerializeObject(object));
        root["objects"] = std::move(objects);
        root["nextObjectId"] = level.nextObjectId;

        nlohmann::json materials = nlohmann::json::array();
        for (const auto& materialPath : level.materialPaths)
            materials.push_back(materialPath);
        root["materialPaths"] = std::move(materials);

        // 不正 UTF-8 は replace で握り、 dump が例外を投げないようにして noexcept 経路を保つ
        return root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool DeserializeLevelFromJson(LevelData& outLevel, std::string_view jsonText, LevelLoadReport* outReport)
    {
        outLevel = LevelData{};
        if (outReport != nullptr)
            *outReport = LevelLoadReport{};

        const nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
        if (root.is_discarded())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "DeserializeLevelFromJson: JSON parse に失敗");
            return false;
        }
        if (!root.is_object())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "DeserializeLevelFromJson: ルートが object でない");
            return false;
        }

        const auto objectsIt = root.find("objects");
        if (objectsIt != root.end() && objectsIt->is_array())
        {
            if (objectsIt->size() > kMaxObjectCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "DeserializeLevelFromJson: object 数 {} が上限 {} を超過",
                             objectsIt->size(),
                             kMaxObjectCount);
                outLevel = LevelData{};
                return false;
            }
            outLevel.objects.reserve(objectsIt->size());
            for (const auto& objectJson : *objectsIt)
                outLevel.objects.push_back(DeserializeObject(objectJson));
        }

        const auto materialsIt = root.find("materialPaths");
        if (materialsIt != root.end() && materialsIt->is_array())
        {
            if (materialsIt->size() > kMaxMaterialPaths)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "DeserializeLevelFromJson: material path 数 {} が上限 {} を超過",
                             materialsIt->size(),
                             kMaxMaterialPaths);
                outLevel = LevelData{};
                return false;
            }
            for (const auto& materialJson : *materialsIt)
            {
                if (!materialJson.is_string())
                    continue;
                std::string materialPath = materialJson.get<std::string>();
                if (materialPath.size() > kMaxMaterialPathLength)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Game,
                                 "DeserializeLevelFromJson: material path が長すぎる ({} > {} byte)",
                                 materialPath.size(),
                                 kMaxMaterialPathLength);
                    outLevel = LevelData{};
                    return false;
                }
                outLevel.materialPaths.push_back(std::move(materialPath));
            }
        }

        // v3 以前の据え置きカメラは別リストだった。読込時に配置物へ変換して objects へ合流させる
        const auto camerasIt = root.find("cameraVolumes");
        if (camerasIt != root.end() && camerasIt->is_array())
        {
            if (camerasIt->size() > kMaxLegacyCameraVolumeCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "DeserializeLevelFromJson: camera volume 数 {} が上限 {} を超過",
                             camerasIt->size(),
                             kMaxLegacyCameraVolumeCount);
                outLevel = LevelData{};
                return false;
            }
            outLevel.objects.reserve(outLevel.objects.size() + camerasIt->size());
            for (const auto& cameraJson : *camerasIt)
                outLevel.objects.push_back(MakeCameraObjectFromLegacyVolume(cameraJson));
        }

        // v4 以前のプレイヤーは spawn 単一値だった。読込時に objects の実体へ変換して合流させる
        // v5 以降でもプレイヤー欠落の手編集ファイルには既定位置で 1 体を合成し、「必ず 1 体」を読込の門で保証する
        const int loadedVersion = ReadInt(root, "formatVersion", 1);
        if (FindPlayerObjectIndex(outLevel) == kNoObjectIndex)
        {
            float spawnX = 0.0f;
            float spawnY = kDefaultPlayerSpawnY;
            float spawnZ = 0.0f;
            float spawnRotationX = 0.0f;
            float spawnRotationY = 0.0f;
            float spawnRotationZ = 0.0f;
            float spawnRotationW = 1.0f;
            ReadVec3(root, "spawn", spawnX, spawnY, spawnZ);
            ReadVec4(root, "spawnRotation", spawnRotationX, spawnRotationY, spawnRotationZ, spawnRotationW);
            if (loadedVersion < 2 && root.contains("spawn"))
            {
                // v1 までの spawn はグリッドセル番号で「そのセルに立つ」 意味だった。 v2 以降は capsule 中心の
                // world 位置なので、 旧コードの床乗せ分を足して中心へ移す
                constexpr float kLegacyStandLift = 0.41f; // capsule halfHeight 0.5 + radius 0.4 + 1cm - cell 半 0.5
                spawnY += kLegacyStandLift;
            }
            outLevel.objects.push_back(
                MakePlayerObject(NS::Math::Vector3{spawnX, spawnY, spawnZ},
                                 NS::Math::Quaternion{spawnRotationX, spawnRotationY, spawnRotationZ, spawnRotationW}));
            if (outReport != nullptr)
                outReport->playerObjectCreated = true;
        }

        // プレイヤーは必ず 1 体。 余分は先頭を正として組まれず、 手編集の重複をここで知らせる
        std::size_t playerCount = 0;
        for (const ObjectInstance& object : outLevel.objects)
        {
            if (IsPlayerObject(object))
                ++playerCount;
        }
        if (playerCount > 1)
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う",
                        playerCount);

        // environment 欄がシーンの見た目を所有する。 中立の既定値の上に読めたキーだけ部分適用する
        const auto environmentIt = root.find("environment");
        if (environmentIt != root.end() && environmentIt->is_object())
        {
            ReadVec3(*environmentIt,
                     "lightDirection",
                     outLevel.environment.lightDirection.x,
                     outLevel.environment.lightDirection.y,
                     outLevel.environment.lightDirection.z);
            ReadVec3(*environmentIt,
                     "lightColor",
                     outLevel.environment.lightColor.x,
                     outLevel.environment.lightColor.y,
                     outLevel.environment.lightColor.z);
            ReadVec3(*environmentIt,
                     "ambientColor",
                     outLevel.environment.ambientColor.x,
                     outLevel.environment.ambientColor.y,
                     outLevel.environment.ambientColor.z);
            const auto skyboxIt = environmentIt->find("skyboxCubemapPath");
            if (skyboxIt != environmentIt->end() && skyboxIt->is_string())
                outLevel.environment.skyboxCubemapPath = skyboxIt->get<std::string>();
        }

        outLevel.nextObjectId = static_cast<std::uint32_t>(ReadInt(root, "nextObjectId", 1));
        // v2 以前は id 無しで全 object が未割当。読込直後に必ず一意化し、以降の経路は id を信頼できる
        EnsureUniqueObjectIds(outLevel);

        // v5 以前の追従カメラは scene 直組みだった。プレイヤー同様、無ければプレイヤーを追う 1 台を
        // 合成して「必ず 1 台」を読込の門で保証する。Target 参照が要るため採番の後に足す
        if (FindFollowCameraObjectIndex(outLevel) == kNoObjectIndex)
        {
            const std::size_t playerIndex = FindPlayerObjectIndex(outLevel);
            const std::uint32_t targetId = [&]() -> std::uint32_t {
                if (playerIndex != kNoObjectIndex)
                    return outLevel.objects[playerIndex].objectId;
                return 0u;
            }();
            outLevel.objects.push_back(MakeFollowCameraObject(targetId));
            EnsureUniqueObjectIds(outLevel);
        }

        // 手編集や参照先削除で宙に浮いた参照は入口で未設定へ戻す。実行時は id 照合の失敗を考えずに済む
        const std::size_t prunedRefs = PruneDanglingObjectRefs(outLevel);
        if (prunedRefs > 0)
            NS_LOG_WARN(::NS::Core::LogCat::Game, "存在しない object を指す参照を {} 件未設定に戻した", prunedRefs);

        return true;
    }

    bool SaveLevelToJsonFile(const LevelData& level, const std::filesystem::path& path) noexcept
    {
        if (level.objects.size() > kMaxObjectCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "SaveLevelToJsonFile: object 数が上限超過 ({} > {})",
                         level.objects.size(),
                         kMaxObjectCount);
            return false;
        }
        if (level.materialPaths.size() > kMaxMaterialPaths)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "SaveLevelToJsonFile: material path 数が上限超過 ({} > {})",
                         level.materialPaths.size(),
                         kMaxMaterialPaths);
            return false;
        }
        // load 側が同じ閾値で拒否するため、 上限超過パスは保存段で弾いて往復不能を防ぐ
        for (const auto& materialPath : level.materialPaths)
        {
            if (materialPath.size() > kMaxMaterialPathLength)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "SaveLevelToJsonFile: material path が長すぎる ({} > {} byte)",
                             materialPath.size(),
                             kMaxMaterialPathLength);
                return false;
            }
        }
        // json の構築 / dump は bad_alloc を投げ得る。 noexcept 契約を守るため捕捉して false に変換する
        try
        {
            const std::string text = SerializeLevelToJson(level);
            if (text.size() > kMaxLevelFileBytes)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "SaveLevelToJsonFile: 出力 file が上限 ({} byte) を超過: {} byte",
                             kMaxLevelFileBytes,
                             text.size());
                return false;
            }

            const auto* raw = reinterpret_cast<const std::byte*>(text.data());
            return ::NS::Core::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size()));
        }
        catch (...)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "SaveLevelToJsonFile: 直列化中に例外を捕捉");
            return false;
        }
    }

    bool LoadLevelFromJsonFile(LevelData& outLevel,
                               const std::filesystem::path& path,
                               LevelLoadReport* outReport) noexcept
    {
        outLevel = LevelData{};

        auto textOpt = ::NS::Core::FileSystem::ReadAllText(path);
        if (!textOpt.has_value())
            return false;

        if (textOpt->size() > kMaxLevelFileBytes)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "LoadLevelFromJsonFile: file が上限 ({} byte) を超えるので reject: {}",
                         kMaxLevelFileBytes,
                         path.string());
            return false;
        }

        // parse 後の json 操作 / LevelData 構築は bad_alloc を投げ得る。 noexcept 契約を守るため捕捉する
        try
        {
            if (!DeserializeLevelFromJson(outLevel, *textOpt, outReport))
            {
                outLevel = LevelData{};
                return false;
            }
        }
        catch (...)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "LoadLevelFromJsonFile: 読込中に例外を捕捉");
            outLevel = LevelData{};
            return false;
        }
        return true;
    }
} // namespace NS::Game::Level
