#include <gtest/gtest.h>

#include <Framework/Graphics/RenderSettings.h>

#include <optional>

namespace
{
    constexpr float kEpsilon = 1e-5f;
}

TEST(RenderSettings, DefaultsMatchCurrentValues)
{
    const NS::Graphics::RenderSettings s{};
    EXPECT_NEAR(s.clearColor.R(), 0.10f, kEpsilon);
    EXPECT_NEAR(s.clearColor.G(), 0.10f, kEpsilon);
    EXPECT_NEAR(s.clearColor.B(), 0.15f, kEpsilon);
    EXPECT_NEAR(s.clearColor.A(), 1.0f, kEpsilon);
    EXPECT_NEAR(s.lightDir.x, -0.3f, kEpsilon);
    EXPECT_NEAR(s.lightDir.y, -1.0f, kEpsilon);
    EXPECT_NEAR(s.lightDir.z, -0.2f, kEpsilon);
    EXPECT_NEAR(s.lightColor.x, 1.0f, kEpsilon);
    EXPECT_NEAR(s.ambientColor.x, 0.2f, kEpsilon);
    EXPECT_NEAR(s.ambientColor.y, 0.2f, kEpsilon);
    EXPECT_NEAR(s.ambientColor.z, 0.2f, kEpsilon);
    EXPECT_TRUE(s.vsync);
}

TEST(RenderSettings, ResolveWithEmptyOverrideReturnsDefaults)
{
    const NS::Graphics::RenderSettings defaults{};
    const NS::Graphics::RenderSettingsOverride over{};
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_NEAR(r.lightDir.x, defaults.lightDir.x, kEpsilon);
    EXPECT_NEAR(r.ambientColor.x, defaults.ambientColor.x, kEpsilon);
    EXPECT_NEAR(r.clearColor.B(), defaults.clearColor.B(), kEpsilon);
    EXPECT_EQ(r.vsync, defaults.vsync);
}

TEST(RenderSettings, ResolveOverridesOnlySpecifiedField)
{
    const NS::Graphics::RenderSettings defaults{};
    NS::Graphics::RenderSettingsOverride over{};
    over.lightDir = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_NEAR(r.lightDir.x, 1.0f, kEpsilon);
    EXPECT_NEAR(r.lightDir.y, 0.0f, kEpsilon);
    EXPECT_NEAR(r.ambientColor.x, defaults.ambientColor.x, kEpsilon);
    EXPECT_NEAR(r.clearColor.B(), defaults.clearColor.B(), kEpsilon);
}

TEST(RenderSettings, ResolveOverridesAllFields)
{
    const NS::Graphics::RenderSettings defaults{};
    NS::Graphics::RenderSettingsOverride over{};
    over.clearColor = NS::Math::Color{0.5f, 0.5f, 0.5f, 1.0f};
    over.lightDir = NS::Math::Vector3{0.0f, -1.0f, 0.0f};
    over.lightColor = NS::Math::Vector3{0.8f, 0.8f, 0.8f};
    over.ambientColor = NS::Math::Vector3{0.3f, 0.3f, 0.3f};
    over.vsync = false;
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_NEAR(r.clearColor.R(), 0.5f, kEpsilon);
    EXPECT_NEAR(r.lightDir.y, -1.0f, kEpsilon);
    EXPECT_NEAR(r.lightColor.x, 0.8f, kEpsilon);
    EXPECT_NEAR(r.ambientColor.x, 0.3f, kEpsilon);
    EXPECT_FALSE(r.vsync);
}

TEST(RenderSettings, ResolveVsyncFalseTakesEffect)
{
    const NS::Graphics::RenderSettings defaults{};
    NS::Graphics::RenderSettingsOverride over{};
    over.vsync = false;
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_FALSE(r.vsync);
}
