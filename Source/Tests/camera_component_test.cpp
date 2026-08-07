#include <gtest/gtest.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CameraComponent.h>

namespace
{
    using NS::Object::CameraComponent;
} // namespace

TEST(CameraComponentTest, DefaultsMatchCameraDefaults)
{
    CameraComponent cc;
    EXPECT_GT(cc.FovY().value, 0.0f);
    EXPECT_GT(cc.Camera().AspectRatio(), 0.0f);
}

TEST(CameraComponentTest, SetPositionAndTargetReflectInCamera)
{
    CameraComponent cc;
    cc.SetPosition({1.0f, 2.0f, 3.0f});
    cc.SetTarget({0.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(cc.Position().x, 1.0f);
    EXPECT_FLOAT_EQ(cc.Position().y, 2.0f);
    EXPECT_FLOAT_EQ(cc.Position().z, 3.0f);
    EXPECT_FLOAT_EQ(cc.Target().x, 0.0f);
}

TEST(CameraComponentTest, ForwardHorizontalReturnsXZUnitVector)
{
    CameraComponent cc;
    cc.SetPosition({0.0f, 5.0f, 0.0f});
    cc.SetTarget({2.0f, 0.0f, 0.0f});

    const auto fwd = cc.ForwardHorizontal();
    EXPECT_FLOAT_EQ(fwd.x, 1.0f);
    EXPECT_FLOAT_EQ(fwd.y, 0.0f);
    EXPECT_FLOAT_EQ(fwd.z, 0.0f);
}

TEST(CameraComponentTest, ForwardHorizontalFallsBackToPlusZWhenDegenerate)
{
    CameraComponent cc;
    cc.SetPosition({0.0f, 5.0f, 0.0f});
    cc.SetTarget({0.0f, 0.0f, 0.0f});

    const auto fwd = cc.ForwardHorizontal();
    EXPECT_FLOAT_EQ(fwd.x, 0.0f);
    EXPECT_FLOAT_EQ(fwd.z, 1.0f);
}

TEST(CameraComponentTest, ViewProjectionDoesNotCrash)
{
    CameraComponent cc;
    cc.SetPosition({0.0f, 1.0f, -3.0f});
    cc.SetTarget({0.0f, 0.0f, 0.0f});
    cc.SetAspectRatio(16.0f / 9.0f);

    const auto vp = cc.ViewProjection();
    (void)vp;
    SUCCEED();
}
