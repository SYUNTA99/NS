#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
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
        constexpr int k_FormatVersion = 9;

        // 読込時の上限。巨大 size / 要素数による メモリ枯渇を防ぐ
        constexpr std::size_t k_MaxSceneFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t k_MaxActorCount = 100'000u;

        // ファイルのアクター 1 体を、知っている欄だけの形へ整える。未知の欄は捨て、fields の中身は素通し
        nlohmann::json NormalizeActorJson(const nlohmann::json& json)
        {
            nlohmann::json object = MakeActorJson();
            if (!json.is_object())
            {
                return object;
            }
            WriteActorHeader(object, ReadActorHeader(json));
            nlohmann::json& parts = ActorJsonClassObjs(object);
            const nlohmann::json& source = ActorJsonClassObjs(json);
            for (nlohmann::json::const_iterator part = source.begin(); part != source.end(); ++part)
            {
                if (part.value().is_object())
                {
                    parts[part.key()] = part.value();
                }
            }
            return object;
        }

        // ファイルの参照は相手の名前で書く。呼ぶ前に名前を一意にしておく。未設定と、一覧に居ない相手は "" で書く
        void WriteRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonActors(scene);
            std::unordered_map<uint32, std::string> names;
            for (const nlohmann::json& object : objects)
            {
                names.emplace(ActorJsonId(object), std::string{ActorJsonName(object)});
            }
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&names](nlohmann::json& ref) {
                    std::string name;
                    if (ref.is_number_unsigned() && ref.get<uint32>() != k_NoActorId)
                    {
                        const std::unordered_map<uint32, std::string>::const_iterator found =
                            names.find(ref.get<uint32>());
                        if (found != names.end())
                        {
                            name = found->second;
                        }
                    }
                    ref = std::move(name);
                });
            }
        }

        // ファイルの参照は相手の名前で書かれている。番号を振った後で id へ直し、辿れない参照は未設定に戻す。
        // 戻した件数を返す。"" と 0 は元から未設定なので数えない
        std::size_t ReadRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonActors(scene);
            std::unordered_map<std::string, uint32> ids;
            for (const nlohmann::json& object : objects)
            {
                ids.emplace(std::string{ActorJsonName(object)}, ActorJsonId(object));
            }
            std::size_t lost = 0;
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&ids, &lost](nlohmann::json& ref) {
                    uint32 id = k_NoActorId;
                    if (ref.is_string() && !ref.get_ref<const std::string&>().empty())
                    {
                        const std::unordered_map<std::string, uint32>::const_iterator found =
                            ids.find(ref.get<std::string>());
                        if (found == ids.end())
                        {
                            ++lost;
                        }
                        else
                        {
                            id = found->second;
                        }
                    }
                    // 数値は番号を書いていた頃の参照。番号は読み込みで振り直すので、相手を決められない
                    else if (ref.is_number() && !(ref.is_number_unsigned() && ref.get<uint32>() == k_NoActorId))
                    {
                        ++lost;
                    }
                    ref = id;
                });
            }
            return lost;
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

    const nlohmann::json& SceneJsonActors(const nlohmann::json& scene) noexcept
    {
        static const nlohmann::json k_Empty = nlohmann::json::array();
        const nlohmann::json* objects = FindFieldValue(scene, "objects");
        if (objects == nullptr || !objects->is_array())
        {
            return k_Empty;
        }
        return *objects;
    }

    nlohmann::json& SceneJsonActors(nlohmann::json& scene)
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

    uint32 SceneJsonNextActorId(const nlohmann::json& scene) noexcept
    {
        return FieldUnsigned(scene, "nextObjectId", 1);
    }

    void SetSceneJsonNextActorId(nlohmann::json& scene, uint32 nextObjectId)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        scene["nextObjectId"] = nextObjectId;
    }

    float ClampMusicVolume(float volume) noexcept
    {
        // Clamp は非数を止めないので、先に弾く
        if (!std::isfinite(volume))
        {
            return SceneEnvironment{}.musicVolume;
        }
        return Clamp(volume, 0.0f, 1.0f);
    }

    SceneEnvironment ReadSceneEnvironment(const nlohmann::json& scene)
    {
        SceneEnvironment environment{};
        const nlohmann::json* found = FindFieldValue(scene, "environment");
        if (found == nullptr || !found->is_object())
        {
            return environment;
        }
        (void)ApplyReflectedFields(&environment, *SceneEnvironment::StaticReflection(), *found);
        environment.musicVolume = ClampMusicVolume(environment.musicVolume);
        return environment;
    }

    void WriteSceneEnvironment(nlohmann::json& scene, const SceneEnvironment& environment)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        nlohmann::json out = SerializeReflectedFields(&environment, *SceneEnvironment::StaticReflection());
        scene["environment"] = std::move(out);
    }

    std::string SceneJsonSkybox(const nlohmann::json& scene)
    {
        return ReadSceneEnvironment(scene).skybox;
    }

    uint32 NextFreeActorId(const nlohmann::json& scene) noexcept
    {
        uint32 next = std::max(SceneJsonNextActorId(scene), 1u);
        for (const nlohmann::json& object : SceneJsonActors(scene))
        {
            next = std::max(next, ActorJsonId(object) + 1);
        }
        return next;
    }

    std::size_t FindActorIndexById(const nlohmann::json& scene, uint32 id) noexcept
    {
        if (id == k_NoActorId)
        {
            return k_NoActorIndex;
        }
        const nlohmann::json& objects = SceneJsonActors(scene);
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (ActorJsonId(objects[i]) == id)
            {
                return i;
            }
        }
        return k_NoActorIndex;
    }

    namespace
    {
        // ファイルの参照は名前で書くので、名前が 1 つに決まることを保存と読込が当てにする
        void EnsureUniqueActorNames(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonActors(scene);
            std::unordered_set<std::string> used;
            std::vector<nlohmann::json*> pending;
            for (nlohmann::json& object : objects)
            {
                const std::string name{ActorJsonName(object)};
                if (name.empty() || !used.insert(name).second)
                {
                    pending.push_back(&object);
                }
            }
            for (nlohmann::json* object : pending)
            {
                const std::string name = MakeUniqueActorName(ActorJsonName(*object), used);
                SetActorJsonName(*object, name);
                used.insert(name);
            }
        }

    } // namespace

    void EnsureUniqueActorIds(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonActors(scene);
        uint32 nextObjectId = NextFreeActorId(scene);
        std::unordered_set<uint32> seen;
        for (nlohmann::json& object : objects)
        {
            const uint32 id = ActorJsonId(object);
            if (id == 0 || !seen.insert(id).second)
            {
                const uint32 fresh = nextObjectId++;
                SetActorJsonId(object, fresh);
                seen.insert(fresh);
            }
        }
        SetSceneJsonNextActorId(scene, nextObjectId);
        EnsureUniqueActorNames(scene);
    }

    std::string MakeUniqueActorName(std::string_view base, const std::unordered_set<std::string>& used)
    {
        std::string name{"Actor"};
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
        // ファイルの中の配置物は名前で見分ける。番号は読み込みのたびに振り直すので書かない。
        // 名前を一意にするのはファイルの中だけで、メモリの名前は変えない
        nlohmann::json out = MakeSceneJson();
        WriteSceneEnvironment(out, ReadSceneEnvironment(scene));
        out["objects"] = SceneJsonActors(scene);
        EnsureUniqueActorNames(out);
        WriteRefsByName(out);
        for (nlohmann::json& object : SceneJsonActors(out))
        {
            SetActorJsonId(object, k_NoActorId);
        }
        out.erase("nextObjectId");

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

        const nlohmann::json& objects = SceneJsonActors(root);
        if (objects.size() > k_MaxActorCount)
        {
            NS_LOG_ERROR(
                Scene, "DeserializeSceneFromJson: object 数 {} が上限 {} を超過", objects.size(), k_MaxActorCount);
            return false;
        }
        // 番号を書いていた頃のファイルの "id" と nextObjectId は読まない。番号は並び順に 1 から振り直す
        nlohmann::json& outObjects = SceneJsonActors(outScene);
        for (const nlohmann::json& objectJson : objects)
        {
            nlohmann::json object = NormalizeActorJson(objectJson);
            SetActorJsonId(object, k_NoActorId);
            outObjects.push_back(std::move(object));
        }

        // environment の知らない欄は読み飛ばす。旧形式の照明の欄は DirectionalLight へ移った
        WriteSceneEnvironment(outScene, ReadSceneEnvironment(root));

        // 番号と名前を一意にしてから、名前で書かれた参照を番号へ直す。以降の経路は番号を信頼できる
        EnsureUniqueActorIds(outScene);
        const std::size_t lostRefs = ReadRefsByName(outScene);
        if (lostRefs > 0)
        {
            NS_LOG_WARN(Scene, "相手へ辿れない参照を {} 件未設定に戻した", lostRefs);
        }

        return true;
    }

    bool SaveSceneToJsonFile(const nlohmann::json& scene, std::string_view path) noexcept
    {
        // 保存前の上限ガード
        const std::size_t objectCount = SceneJsonActors(scene).size();
        if (objectCount > k_MaxActorCount)
        {
            NS_LOG_ERROR(Scene, "SaveSceneToJsonFile: object 数が上限超過 ({} > {})", objectCount, k_MaxActorCount);
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
        // 書く途中で失敗しても前の .scene を残す
        return OS::FileSystem::ReplaceAllBytes(path, std::span<const std::byte>(raw, text.size()));
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

    namespace
    {
        constexpr std::size_t k_MinLevelNameLength = 1;
        constexpr std::size_t k_MaxLevelNameLength = 200;

        constexpr std::array<std::string_view, 22> k_ReservedLevelNames = {
            "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
            "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

        [[nodiscard]] std::string ToUpperAscii(std::string_view text) noexcept
        {
            std::string upper;
            upper.reserve(text.size());
            for (char c : text)
            {
                upper.push_back(static_cast<char>(std::toupper(static_cast<uint8>(c))));
            }
            return upper;
        }

        [[nodiscard]] bool IsReservedLevelName(std::string_view name) noexcept
        {
            const std::string upper = ToUpperAscii(name);
            return std::find(k_ReservedLevelNames.begin(), k_ReservedLevelNames.end(), upper) !=
                   k_ReservedLevelNames.end();
        }

        // 区切りの無い 1 つの名前を検証する。使えなければ空文字列
        [[nodiscard]] std::string SanitizeLevelName(std::string_view name) noexcept
        {
            if (name.size() < k_MinLevelNameLength || name.size() > k_MaxLevelNameLength)
            {
                return "";
            }
            for (char c : name)
            {
                const bool isAlnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                const bool isAllowedPunct = (c == '_' || c == '-' || c == ' ');
                if (!isAlnum && !isAllowedPunct)
                {
                    return "";
                }
            }
            if (name.find("..") != std::string_view::npos)
            {
                return "";
            }
            if (name.front() == ' ' || name.back() == ' ')
            {
                return "";
            }
            if (IsReservedLevelName(name))
            {
                return "";
            }
            return std::string{name};
        }
    } // namespace

    std::string SanitizeLevelPath(std::string_view relativePath) noexcept
    {
        if (relativePath.empty())
        {
            return "";
        }

        std::string result;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t slash = relativePath.find('/', start);
            // 末尾セグメントは区切りが無いので文字列末尾まで
            const std::size_t end = std::min(slash, relativePath.size());
            const std::string_view segment = relativePath.substr(start, end - start);
            if (SanitizeLevelName(segment).empty())
            {
                return "";
            }
            if (!result.empty())
            {
                result.push_back('/');
            }
            result.append(segment);
            if (slash == std::string_view::npos)
            {
                break;
            }
            start = slash + 1;
        }
        return result;
    }

    std::string QualifyLevelPath(std::string_view sanitizedPath) noexcept
    {
        if (sanitizedPath.empty() || sanitizedPath.find('/') != std::string_view::npos)
        {
            return std::string{sanitizedPath};
        }
        return "Scenes/" + std::string{sanitizedPath};
    }

    std::optional<std::string> BuildLevelPath(std::string_view name) noexcept
    {
        const std::string safe = SanitizeLevelPath(name);
        if (safe.empty())
        {
            return std::nullopt;
        }
        return OS::FileSystem::ResolveUnder(OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets"),
                                            QualifyLevelPath(safe) + ".scene");
    }

    std::string LevelNameOfScenePath(std::string_view scenePath) noexcept
    {
        constexpr std::string_view k_AssetsPrefix = "Assets/";
        if (!scenePath.starts_with(k_AssetsPrefix))
        {
            return "";
        }
        const std::string extension = OS::FileSystem::Extension(scenePath);
        if (ToUpperAscii(extension) != ".SCENE")
        {
            return "";
        }
        const std::string_view name =
            scenePath.substr(k_AssetsPrefix.size(), scenePath.size() - k_AssetsPrefix.size() - extension.size());
        return SanitizeLevelPath(name);
    }
} // namespace NS
