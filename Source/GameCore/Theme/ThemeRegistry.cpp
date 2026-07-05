#include "Game/Theme/ThemeRegistry.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include "ThirdParty/nlohmann/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

using namespace NS::Game::Theme;

namespace
{
    /// Get が返す実体。値の出所は `.asset` ファイルだけで、読込前と読込失敗分は中立の ThemeData{} のまま
    std::array<ThemeData, 5>& MutableThemes()
    {
        static std::array<ThemeData, 5> s_themes{};
        return s_themes;
    }

    /// ThemeId と 1:1 の固定ファイル名。並びは ThemeId の値順
    constexpr std::array<const char*, 5> kThemeFileNames = {
        "grass.asset",
        "cave.asset",
        "snow.asset",
        "lava.asset",
        "sky.asset",
    };

    /// 汎用データ資産の種別欄。 雛形はこの値でないと無視して中立の既定値に倒す
    constexpr std::string_view kThemeAssetType = "theme";

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

    /// 種別欄が雛形のものか。 `.asset` は先頭に `"type"` を要求し、 欠落や不一致は雛形として扱わない
    [[nodiscard]] bool IsThemeAsset(const nlohmann::json& json, const char* fileName)
    {
        const auto it = json.find("type");
        if (it == json.end() || !it->is_string() || it->get<std::string>() != kThemeAssetType)
        {
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "{}: 'type' が \"{}\" でないため雛形として読まず中立の既定値を使う",
                        fileName,
                        kThemeAssetType);
            return false;
        }
        return true;
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

    void LoadThemesFromDirectory(const std::filesystem::path& directory)
    {
        for (std::size_t i = 0; i < kThemeFileNames.size(); ++i)
        {
            // 毎回中立の既定値から始めて読めた分だけ上書きする。壊れたテーマが前回値を引きずらない
            ThemeData theme{};

            const std::filesystem::path filePath = directory / kThemeFileNames[i];
            const auto text = ::NS::Core::FileSystem::ReadAllText(filePath);
            if (!text.has_value())
            {
                NS_LOG_WARN(
                    ::NS::Core::LogCat::Game, "テーマファイルが読めないため中立の既定値を使う: {}", filePath.string());
            }
            else
            {
                // 例外を投げない解析にして、壊れたファイル 1 個で起動列を止めない
                const nlohmann::json json = nlohmann::json::parse(*text, nullptr, false);
                if (json.is_discarded())
                    NS_LOG_WARN(::NS::Core::LogCat::Game,
                                "テーマファイルの JSON 解析に失敗したため中立の既定値を使う: {}",
                                filePath.string());
                else if (IsThemeAsset(json, kThemeFileNames[i]))
                    ApplyThemeJson(json, theme, kThemeFileNames[i]);
            }

            MutableThemes()[i] = std::move(theme);
        }
    }

    NS::Game::Level::LevelEnvironment MakeEnvironmentFromTheme(const ThemeData& theme)
    {
        NS::Game::Level::LevelEnvironment environment{};
        environment.lightDirection = theme.lightDirection;
        environment.lightColor = theme.lightColor;
        environment.ambientColor = theme.ambientColor;
        // skybox パスは JSON 素直な文字列で持つため '/' 区切りへ正規化して写す
        environment.skyboxCubemapPath = theme.skyboxCubemapPath.generic_string();
        environment.blockTextureBaseSlice = theme.blockTextureArrayBaseSlice;
        return environment;
    }
} // namespace NS::Game::Theme
