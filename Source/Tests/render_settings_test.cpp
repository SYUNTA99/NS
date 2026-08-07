#include <Runtime/Graphics/RenderSettings.h>
#include <gtest/gtest.h>

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
