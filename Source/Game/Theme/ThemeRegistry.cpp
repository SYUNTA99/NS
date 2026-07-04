#include "Game/Theme/ThemeRegistry.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include "ThirdParty/nlohmann/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

using namespace NS::Game::Theme;

namespace
{
    /// 組み込み既定値。ファイルが欠けても壊れてもこの値で起動できる退避先
    const std::array<ThemeData, 5>& BuiltinThemes()
    {
        static const std::array<ThemeData, 5> kThemes = [] {
            std::array<ThemeData, 5> t{};

            t[0].displayName = "Grass";
            t[0].blockTextureArrayBaseSlice = 0;
            t[0].skyboxCubemapPath = "Assets/Skybox/kurt/";
            t[0].lightDirection = NS::Math::Vector3{-0.3f, -1.0f, -0.2f};
            t[0].lightColor = NS::Math::Vector3{1.0f, 1.0f, 0.95f};
            t[0].ambientColor = NS::Math::Vector3{0.25f, 0.30f, 0.20f};

            t[1].displayName = "Cave";
            t[1].blockTextureArrayBaseSlice = 8;
            t[1].skyboxCubemapPath = "Assets/Skybox/kurt/";
            t[1].lightDirection = NS::Math::Vector3{-0.2f, -1.0f, 0.1f};
            t[1].lightColor = NS::Math::Vector3{0.4f, 0.45f, 0.6f};
            t[1].ambientColor = NS::Math::Vector3{0.10f, 0.10f, 0.15f};

            t[2].displayName = "Snow";
            t[2].blockTextureArrayBaseSlice = 16;
            t[2].skyboxCubemapPath = "Assets/Skybox/kurt/";
            t[2].lightDirection = NS::Math::Vector3{-0.3f, -1.0f, -0.2f};
            t[2].lightColor = NS::Math::Vector3{1.0f, 1.0f, 1.1f};
            t[2].ambientColor = NS::Math::Vector3{0.40f, 0.45f, 0.55f};

            t[3].displayName = "Lava";
            t[3].blockTextureArrayBaseSlice = 24;
            t[3].skyboxCubemapPath = "Assets/Skybox/kurt/";
            t[3].lightDirection = NS::Math::Vector3{-0.2f, -1.0f, 0.0f};
            t[3].lightColor = NS::Math::Vector3{1.3f, 0.8f, 0.4f};
            t[3].ambientColor = NS::Math::Vector3{0.35f, 0.15f, 0.10f};

            t[4].displayName = "Sky";
            t[4].blockTextureArrayBaseSlice = 32;
            t[4].skyboxCubemapPath = "Assets/Skybox/kurt/";
            t[4].lightDirection = NS::Math::Vector3{-0.3f, -1.0f, -0.2f};
            t[4].lightColor = NS::Math::Vector3{1.1f, 1.0f, 1.0f};
            t[4].ambientColor = NS::Math::Vector3{0.40f, 0.50f, 0.70f};

            return t;
        }();
        return kThemes;
    }

    /// Get が返す実体。起動時は組み込み既定値と同値で、LoadThemesFromDirectory が中身を差し替える
    std::array<ThemeData, 5>& MutableThemes()
    {
        static std::array<ThemeData, 5> s_themes = BuiltinThemes();
        return s_themes;
    }

    /// ThemeId と 1:1 の固定ファイル名。並びは ThemeId の値順
    constexpr std::array<const char*, 5> kThemeFileNames = {
        "grass.theme",
        "cave.theme",
        "snow.theme",
        "lava.theme",
        "sky.theme",
    };

    /// key が数値 3 要素の配列なら outValue へ写す。無ければ何もせず、型不正は警告して既定値のまま
    void ReadVector3(const nlohmann::json& json, const char* key, NS::Math::Vector3& outValue, const char* fileName)
    {
        const auto it = json.find(key);
        if (it == json.end())
            return;
        if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() ||
            !(*it)[2].is_number())
        {
            NS_LOG_WARN(::NS::Core::LogCat::Game, "{}: '{}' は数値 3 要素の配列でないため既定値のまま", fileName, key);
            return;
        }
        outValue = NS::Math::Vector3{(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    }

    /// 解析済み JSON の既知キーだけを theme へ写す。未知キーは前方互換のため無視する
    void ApplyThemeJson(const nlohmann::json& json, ThemeData& theme, const char* fileName)
    {
        if (const auto it = json.find("displayName"); it != json.end())
        {
            if (it->is_string())
                theme.displayName = it->get<std::string>();
            else
                NS_LOG_WARN(::NS::Core::LogCat::Game, "{}: 'displayName' は文字列でないため既定値のまま", fileName);
        }
        if (const auto it = json.find("blockTextureArrayBaseSlice"); it != json.end())
        {
            if (it->is_number_unsigned() && it->get<std::uint64_t>() <= UINT16_MAX)
                theme.blockTextureArrayBaseSlice = static_cast<std::uint16_t>(it->get<std::uint64_t>());
            else
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "{}: 'blockTextureArrayBaseSlice' は 0..65535 の整数でないため既定値のまま",
                            fileName);
        }
        if (const auto it = json.find("skyboxCubemapPath"); it != json.end())
        {
            if (it->is_string())
                theme.skyboxCubemapPath = std::filesystem::path(it->get<std::string>());
            else
                NS_LOG_WARN(
                    ::NS::Core::LogCat::Game, "{}: 'skyboxCubemapPath' は文字列でないため既定値のまま", fileName);
        }
        ReadVector3(json, "lightDirection", theme.lightDirection, fileName);
        ReadVector3(json, "lightColor", theme.lightColor, fileName);
        ReadVector3(json, "ambientColor", theme.ambientColor, fileName);
    }
} // namespace

namespace NS::Game::Theme
{
    const ThemeData& Get(ThemeId id) noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        if (index >= static_cast<std::size_t>(ThemeId::Count))
            return MutableThemes()[0];
        return MutableThemes()[index];
    }

    const ThemeData& Get(std::uint16_t levelDataThemeId) noexcept
    {
        return Get(static_cast<ThemeId>(levelDataThemeId));
    }

    void LoadThemesFromDirectory(const std::filesystem::path& directory)
    {
        for (std::size_t i = 0; i < kThemeFileNames.size(); ++i)
        {
            // 毎回組み込み既定値から始めて読めた分だけ上書きする。壊れたテーマが前回値を引きずらない
            ThemeData theme = BuiltinThemes()[i];

            const std::filesystem::path filePath = directory / kThemeFileNames[i];
            const auto text = ::NS::Core::FileSystem::ReadAllText(filePath);
            if (!text.has_value())
            {
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "テーマファイルが読めないため組み込み既定値を使う: {}",
                            filePath.string());
            }
            else
            {
                // 例外を投げない解析にして、壊れたファイル 1 個で起動列を止めない
                const nlohmann::json json = nlohmann::json::parse(*text, nullptr, false);
                if (json.is_discarded())
                    NS_LOG_WARN(::NS::Core::LogCat::Game,
                                "テーマファイルの JSON 解析に失敗したため組み込み既定値を使う: {}",
                                filePath.string());
                else
                    ApplyThemeJson(json, theme, kThemeFileNames[i]);
            }

            MutableThemes()[i] = std::move(theme);
        }
    }
} // namespace NS::Game::Theme
