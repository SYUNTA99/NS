#include <Runtime/Graphics/RenderSettings.h>
#include <gtest/gtest.h>
#include <optional>

namespace
{
    constexpr float k_Epsilon = 1e-5f;
}

TEST(RenderSettings, DefaultsMatchCurrentValues)
{
    const NS::Graphics::RenderSettings s{};
    EXPECT_NEAR(s.clearColor.R(), 0.10f, k_Epsilon);
    EXPECT_NEAR(s.clearColor.G(), 0.10f, k_Epsilon);
    EXPECT_NEAR(s.clearColor.B(), 0.15f, k_Epsilon);
    EXPECT_NEAR(s.clearColor.A(), 1.0f, k_Epsilon);
    EXPECT_NEAR(s.lightDir.x, -0.3f, k_Epsilon);
    EXPECT_NEAR(s.lightDir.y, -1.0f, k_Epsilon);
    EXPECT_NEAR(s.lightDir.z, -0.2f, k_Epsilon);
    EXPECT_NEAR(s.lightColor.x, 1.0f, k_Epsilon);
    EXPECT_NEAR(s.lightColor.y, 1.0f, k_Epsilon);
    EXPECT_NEAR(s.lightColor.z, 1.0f, k_Epsilon);
    EXPECT_NEAR(s.ambientColor.x, 0.30f, k_Epsilon);
    EXPECT_NEAR(s.ambientColor.y, 0.34f, k_Epsilon);
    EXPECT_NEAR(s.ambientColor.z, 0.40f, k_Epsilon);
    EXPECT_NEAR(s.groundColor.x, 0.24f, k_Epsilon);
    EXPECT_NEAR(s.groundColor.y, 0.21f, k_Epsilon);
    EXPECT_NEAR(s.groundColor.z, 0.18f, k_Epsilon);
    EXPECT_NEAR(s.exposure, 1.35f, k_Epsilon);
}

// 空側と地面側は別の色。 同じにすると影の中で面の向きが読めなくなる
TEST(RenderSettings, SkyAndGroundAmbientDiffer)
{
    const NS::Graphics::RenderSettings s{};
    EXPECT_GT(s.ambientColor.z, s.groundColor.z);
}

TEST(RenderSettings, ResolveWithEmptyOverrideReturnsDefaults)
{
    const NS::Graphics::RenderSettings defaults{};
    const NS::Graphics::RenderSettingsOverride over{};
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_NEAR(r.lightDir.x, defaults.lightDir.x, k_Epsilon);
    EXPECT_NEAR(r.ambientColor.x, defaults.ambientColor.x, k_Epsilon);
    EXPECT_NEAR(r.clearColor.B(), defaults.clearColor.B(), k_Epsilon);
}

TEST(RenderSettings, ResolveOverridesOnlySpecifiedField)
{
    const NS::Graphics::RenderSettings defaults{};
    NS::Graphics::RenderSettingsOverride over{};
    over.lightDir = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_NEAR(r.lightDir.x, 1.0f, k_Epsilon);
    EXPECT_NEAR(r.lightDir.y, 0.0f, k_Epsilon);
    EXPECT_NEAR(r.ambientColor.x, defaults.ambientColor.x, k_Epsilon);
    EXPECT_NEAR(r.clearColor.B(), defaults.clearColor.B(), k_Epsilon);
}

TEST(RenderSettings, ResolveOverridesAllFields)
{
    const NS::Graphics::RenderSettings defaults{};
    NS::Graphics::RenderSettingsOverride over{};
    over.clearColor = NS::Math::Color{0.5f, 0.5f, 0.5f, 1.0f};
    over.lightDir = NS::Math::Vector3{0.0f, -1.0f, 0.0f};
    over.lightColor = NS::Math::Vector3{0.8f, 0.8f, 0.8f};
    over.ambientColor = NS::Math::Vector3{0.3f, 0.3f, 0.3f};
    over.groundColor = NS::Math::Vector3{0.1f, 0.1f, 0.1f};
    const NS::Graphics::RenderSettings r = NS::Graphics::Resolve(defaults, over);
    EXPECT_NEAR(r.clearColor.R(), 0.5f, k_Epsilon);
    EXPECT_NEAR(r.lightDir.y, -1.0f, k_Epsilon);
    EXPECT_NEAR(r.lightColor.x, 0.8f, k_Epsilon);
    EXPECT_NEAR(r.ambientColor.x, 0.3f, k_Epsilon);
    EXPECT_NEAR(r.groundColor.x, 0.1f, k_Epsilon);
}
