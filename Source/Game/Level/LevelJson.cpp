#include "Game/Level/LevelJson.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Math/Math.h"
#include "Game/Level/LevelData.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>

// json.hpp は /W4 で警告が出るため、 この翻訳単位でだけ警告を抑止して取り込む
#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Game::Level
{
    namespace
    {
        /// セーブフォーマットのバージョン。 binary 時代の major/minor を 1 整数へ置換した
        constexpr int kFormatVersion = 1;

        /// 旧 "kind" だけで配置物を表す古い JSON を読込時に持ち回る移行 placeholder の型名
        /// MigrateLegacyLevel が実 component へ展開して取り除くため、 保存時はこの型を書き出さない
        constexpr const char* kLegacyKindTypeName = "LegacyKind";

        /// 読込時の上限。 巨大 size / 要素数による memory exhaustion を防ぐ (binary 版から移植)
        constexpr std::size_t kMaxLevelFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t kMaxObjectCount = 100'000u;
        constexpr std::size_t kMaxMaterialPaths = 4'096u;
        constexpr std::size_t kMaxMaterialPathLength = 1'024u;
        constexpr std::size_t kMaxCameraVolumeCount = 4'096u;

        nlohmann::json Vec3Json(float x, float y, float z)
        {
            return nlohmann::json{x, y, z};
        }

        nlohmann::json Vec4Json(float x, float y, float z, float w)
        {
            return nlohmann::json{x, y, z, w};
        }

        /// parent[key] が長さ 3 の数値配列なら x/y/z へ書き込む。 不在 / 型不一致は据え置き (前方互換)
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

        /// parent[key] が長さ 4 の数値配列なら x/y/z/w へ書き込む。 不在 / 型不一致は据え置き (前方互換)
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

        /// parent[key] が数値なら int で返す。 不在 / 型不一致は fallback (手編集の 1.0 形式も拾う)
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
                const auto& v = std::get<NS::Math::Vector3>(field.value);
                return Vec3Json(v.x, v.y, v.z);
            }
            case 4:
                return std::get<std::string>(field.value);
            default:
                return nlohmann::json{};
            }
        }

        /// JSON 値から FieldValue の variant を推論する。 bool→bool / 小数→float / 整数→int / 配列3→Vector3 /
        /// 文字列→string。 いずれにも合わなければ何も積まない (前方互換)
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
                out = FieldValue{
                    name, NS::Math::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()}};
                return true;
            }
            return false;
        }

        nlohmann::json SerializeComponentData(const ComponentData& component)
        {
            nlohmann::json out;
            out["type"] = component.typeName;
            out["fields"] = ComponentFieldsToJson(component);
            return out;
        }

        ComponentData DeserializeComponentData(const nlohmann::json& j)
        {
            ComponentData component;
            if (!j.is_object())
                return component;
            const auto typeIt = j.find("type");
            if (typeIt != j.end() && typeIt->is_string())
                component.typeName = typeIt->get<std::string>();
            const auto fieldsIt = j.find("fields");
            if (fieldsIt != j.end() && fieldsIt->is_object())
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

        nlohmann::json SerializeObject(const ObjectInstance& o)
        {
            nlohmann::json transform;
            transform["pos"] = Vec3Json(o.positionX, o.positionY, o.positionZ);
            transform["rot"] = Vec4Json(o.rotationX, o.rotationY, o.rotationZ, o.rotationW);
            transform["scale"] = Vec3Json(o.scaleX, o.scaleY, o.scaleZ);

            nlohmann::json collider;
            collider["shape"] = static_cast<int>(o.shapeCollider);
            collider["halfExtents"] = Vec3Json(o.colliderHalfExtentsX, o.colliderHalfExtentsY, o.colliderHalfExtentsZ);
            collider["offset"] = Vec3Json(o.colliderOffsetX, o.colliderOffsetY, o.colliderOffsetZ);
            collider["rotation"] =
                Vec4Json(o.colliderRotationX, o.colliderRotationY, o.colliderRotationZ, o.colliderRotationW);

            nlohmann::json components = nlohmann::json::array();
            for (const auto& component : o.components)
            {
                // 移行 placeholder は永続させない。 通常は MigrateLegacyLevel が読込後に取り除くが、
                // 万一残っていても保存段で弾いてファイルへ漏らさない
                if (component.typeName == kLegacyKindTypeName)
                    continue;
                components.push_back(SerializeComponentData(component));
            }

            nlohmann::json out;
            out["transform"] = std::move(transform);
            out["materialIndex"] = static_cast<int>(o.materialIndex);
            out["flags"] = static_cast<int>(o.flags);
            out["reserved1"] = static_cast<int>(o.reserved1);
            out["collider"] = std::move(collider);
            out["components"] = std::move(components);
            return out;
        }

        ObjectInstance DeserializeObject(const nlohmann::json& j)
        {
            ObjectInstance o{};
            if (!j.is_object())
                return o;

            const auto transformIt = j.find("transform");
            if (transformIt != j.end() && transformIt->is_object())
            {
                ReadVec3(*transformIt, "pos", o.positionX, o.positionY, o.positionZ);
                ReadVec4(*transformIt, "rot", o.rotationX, o.rotationY, o.rotationZ, o.rotationW);
                ReadVec3(*transformIt, "scale", o.scaleX, o.scaleY, o.scaleZ);
            }

            // 旧 "kind" は構造体には載らない。 ローカルで読み、 components 不在のとき LegacyKind placeholder へ移す
            const int legacyKind = ReadInt(j, "kind", 0);
            o.materialIndex = static_cast<std::int16_t>(ReadInt(j, "materialIndex", o.materialIndex));
            o.flags = static_cast<std::uint8_t>(ReadInt(j, "flags", o.flags));
            o.reserved1 = static_cast<std::uint16_t>(ReadInt(j, "reserved1", o.reserved1));

            const auto colliderIt = j.find("collider");
            if (colliderIt != j.end() && colliderIt->is_object())
            {
                o.shapeCollider = static_cast<std::uint8_t>(ReadInt(*colliderIt, "shape", o.shapeCollider));
                ReadVec3(
                    *colliderIt, "halfExtents", o.colliderHalfExtentsX, o.colliderHalfExtentsY, o.colliderHalfExtentsZ);
                ReadVec3(*colliderIt, "offset", o.colliderOffsetX, o.colliderOffsetY, o.colliderOffsetZ);
                ReadVec4(*colliderIt,
                         "rotation",
                         o.colliderRotationX,
                         o.colliderRotationY,
                         o.colliderRotationZ,
                         o.colliderRotationW);
            }

            const auto componentsIt = j.find("components");
            const bool hasComponentsArray = componentsIt != j.end() && componentsIt->is_array();
            if (hasComponentsArray)
            {
                o.components.reserve(componentsIt->size());
                for (const auto& cj : *componentsIt)
                    o.components.push_back(DeserializeComponentData(cj));
            }
            else if (legacyKind != 0)
            {
                // components 配列を持たず kind だけで配置物を表す古い JSON は移行 placeholder を 1 つ積む
                // MigrateLegacyLevel が読込後に実 component へ展開する。 現フォーマット (空配列でも array あり)
                // は対象外
                ComponentData legacy;
                legacy.typeName = kLegacyKindTypeName;
                legacy.fields.push_back(FieldValue{"id", legacyKind});
                o.components.push_back(std::move(legacy));
            }
            return o;
        }

        nlohmann::json SerializeCameraVolume(const CameraVolume& c)
        {
            nlohmann::json out;
            out["cameraPosition"] = Vec3Json(c.cameraPositionX, c.cameraPositionY, c.cameraPositionZ);
            out["lookTarget"] = Vec3Json(c.lookTargetX, c.lookTargetY, c.lookTargetZ);
            out["triggerCenter"] = Vec3Json(c.triggerCenterX, c.triggerCenterY, c.triggerCenterZ);
            out["triggerExtent"] = Vec3Json(c.triggerExtentX, c.triggerExtentY, c.triggerExtentZ);
            out["priority"] = c.priority;
            out["lookAtPlayer"] = static_cast<int>(c.lookAtPlayer);
            out["reserved0"] = static_cast<int>(c.reserved0);
            out["reserved1"] = static_cast<int>(c.reserved1);
            return out;
        }

        CameraVolume DeserializeCameraVolume(const nlohmann::json& j)
        {
            CameraVolume c{};
            if (!j.is_object())
                return c;
            ReadVec3(j, "cameraPosition", c.cameraPositionX, c.cameraPositionY, c.cameraPositionZ);
            ReadVec3(j, "lookTarget", c.lookTargetX, c.lookTargetY, c.lookTargetZ);
            ReadVec3(j, "triggerCenter", c.triggerCenterX, c.triggerCenterY, c.triggerCenterZ);
            ReadVec3(j, "triggerExtent", c.triggerExtentX, c.triggerExtentY, c.triggerExtentZ);
            c.priority = ReadInt(j, "priority", c.priority);
            c.lookAtPlayer = static_cast<std::uint8_t>(ReadInt(j, "lookAtPlayer", c.lookAtPlayer));
            c.reserved0 = static_cast<std::uint8_t>(ReadInt(j, "reserved0", c.reserved0));
            c.reserved1 = static_cast<std::uint16_t>(ReadInt(j, "reserved1", c.reserved1));
            return c;
        }
    } // namespace

    nlohmann::json ComponentFieldsToJson(const ComponentData& component)
    {
        nlohmann::json fields = nlohmann::json::object();
        for (const auto& field : component.fields)
            fields[field.name] = FieldValueToJson(field);
        return fields;
    }

    std::string SerializeLevelToJson(const LevelData& level)
    {
        nlohmann::json root;
        root["formatVersion"] = kFormatVersion;

        nlohmann::json meta;
        meta["themeId"] = static_cast<int>(level.themeId);
        meta["bgmId"] = static_cast<int>(level.bgmId);
        meta["coinThreshold"] = static_cast<int>(level.coinThreshold);
        meta["timeLimitSeconds"] = static_cast<int>(level.timeLimitSeconds);
        root["meta"] = std::move(meta);

        root["spawn"] = nlohmann::json{
            static_cast<int>(level.spawnX), static_cast<int>(level.spawnY), static_cast<int>(level.spawnZ)};

        nlohmann::json objects = nlohmann::json::array();
        for (const auto& object : level.objects)
            objects.push_back(SerializeObject(object));
        root["objects"] = std::move(objects);

        nlohmann::json materials = nlohmann::json::array();
        for (const auto& materialPath : level.materialPaths)
            materials.push_back(materialPath);
        root["materialPaths"] = std::move(materials);

        nlohmann::json cameras = nlohmann::json::array();
        for (const auto& camera : level.cameraVolumes)
            cameras.push_back(SerializeCameraVolume(camera));
        root["cameraVolumes"] = std::move(cameras);

        // 不正 UTF-8 は replace で握り、 dump が例外を投げないようにする (noexcept 経路を保つ)
        return root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool DeserializeLevelFromJson(LevelData& outLevel, std::string_view jsonText)
    {
        outLevel = LevelData{};

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
            for (const auto& oj : *objectsIt)
                outLevel.objects.push_back(DeserializeObject(oj));
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
            for (const auto& mj : *materialsIt)
            {
                if (!mj.is_string())
                    continue;
                std::string materialPath = mj.get<std::string>();
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

        const auto camerasIt = root.find("cameraVolumes");
        if (camerasIt != root.end() && camerasIt->is_array())
        {
            if (camerasIt->size() > kMaxCameraVolumeCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "DeserializeLevelFromJson: camera volume 数 {} が上限 {} を超過",
                             camerasIt->size(),
                             kMaxCameraVolumeCount);
                outLevel = LevelData{};
                return false;
            }
            outLevel.cameraVolumes.reserve(camerasIt->size());
            for (const auto& cj : *camerasIt)
                outLevel.cameraVolumes.push_back(DeserializeCameraVolume(cj));
        }

        const auto spawnIt = root.find("spawn");
        if (spawnIt != root.end() && spawnIt->is_array() && spawnIt->size() == 3u && (*spawnIt)[0].is_number() &&
            (*spawnIt)[1].is_number() && (*spawnIt)[2].is_number())
        {
            outLevel.spawnX = static_cast<std::int16_t>((*spawnIt)[0].get<int>());
            outLevel.spawnY = static_cast<std::int16_t>((*spawnIt)[1].get<int>());
            outLevel.spawnZ = static_cast<std::int16_t>((*spawnIt)[2].get<int>());
        }

        const auto metaIt = root.find("meta");
        if (metaIt != root.end() && metaIt->is_object())
        {
            outLevel.themeId = static_cast<std::uint16_t>(ReadInt(*metaIt, "themeId", outLevel.themeId));
            outLevel.bgmId = static_cast<std::uint16_t>(ReadInt(*metaIt, "bgmId", outLevel.bgmId));
            outLevel.coinThreshold =
                static_cast<std::uint16_t>(ReadInt(*metaIt, "coinThreshold", outLevel.coinThreshold));
            outLevel.timeLimitSeconds =
                static_cast<std::uint16_t>(ReadInt(*metaIt, "timeLimitSeconds", outLevel.timeLimitSeconds));
        }

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
        if (level.cameraVolumes.size() > kMaxCameraVolumeCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "SaveLevelToJsonFile: camera volume 数が上限超過 ({} > {})",
                         level.cameraVolumes.size(),
                         kMaxCameraVolumeCount);
            return false;
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

    bool LoadLevelFromJsonFile(LevelData& outLevel, const std::filesystem::path& path) noexcept
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
            if (!DeserializeLevelFromJson(outLevel, *textOpt))
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
