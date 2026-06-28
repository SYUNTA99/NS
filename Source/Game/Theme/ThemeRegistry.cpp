#include "Game/Theme/ThemeRegistry.h"

#include <array>
#include <cstddef>

namespace
{
    /// 5 テーマの静的レジストリ。 baseSlice / lightColor / ambientColor を定義する
    const std::array<ThemeData, 5>& Themes()
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
} // namespace

namespace ThemeRegistry
{
    const ThemeData& Get(ThemeId id) noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        if (index >= static_cast<std::size_t>(ThemeId::Count))
            return Themes()[0];
        return Themes()[index];
    }

    const ThemeData& Get(std::uint16_t levelDataThemeId) noexcept
    {
        return Get(static_cast<ThemeId>(levelDataThemeId));
    }
} // namespace ThemeRegistry
