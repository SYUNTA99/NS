#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/CameraComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ThirdPersonFollowComponent.h>
#include <Framework/Scene/Transform.h>

#include <cmath>

namespace
{
    using NS::Scene::CameraComponent;
    using NS::Scene::GameObject;
    using NS::Scene::ThirdPersonFollowComponent;

    constexpr float kDt = 1.0f / 60.0f;
} // namespace

class ThirdPersonFollowTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kDt); }
};

TEST_F(ThirdPersonFollowTest, ConstructsWithNullTarget)
{
    ThirdPersonFollowComponent follow(nullptr);
    EXPECT_EQ(follow.Target(), nullptr);
    EXPECT_TRUE(follow.IsActive());
}

TEST_F(ThirdPersonFollowTest, OnUpdateNoOpWhenCameraIsNull)
{
    GameObject obj;
    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>(&obj.Root());
    follow.OnUpdate();
    SUCCEED();
}

TEST_F(ThirdPersonFollowTest, OnUpdateNoOpWhenTargetIsNull)
{
    CameraComponent cc;
    ThirdPersonFollowComponent follow(nullptr);
    follow.SetCamera(&cc);

    const auto posBefore = cc.Position();
    follow.OnUpdate();
    const auto posAfter = cc.Position();

    EXPECT_FLOAT_EQ(posBefore.x, posAfter.x);
    EXPECT_FLOAT_EQ(posBefore.y, posAfter.y);
    EXPECT_FLOAT_EQ(posBefore.z, posAfter.z);
}

TEST_F(ThirdPersonFollowTest, UpdatesCameraPositionBehindTarget)
{
    GameObject obj;
    obj.Root().SetPosition({0.0f, 0.0f, 0.0f});

    CameraComponent cc;
    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>(&obj.Root());
    follow.SetCamera(&cc);
    follow.SetDistance(5.0f);

    for (int i = 0; i < 60; ++i)
        follow.OnUpdate();

    // OnUpdate は state mutation のみ (yaw/pitch/distance)、 camera position は
    // ApplyCameraTransform で render frame ごとに反映する設計 (jitter 回避)
    follow.ApplyCameraTransform(1.0f);

    const auto pos = cc.Position();
    EXPECT_NEAR(pos.x, 0.0f, 0.1f);
    EXPECT_GT(pos.y, 1.0f);
    EXPECT_LT(pos.z, -3.0f);

    const auto tgt = cc.Target();
    EXPECT_NEAR(tgt.y, 1.2f, 0.01f);
}

TEST_F(ThirdPersonFollowTest, SetFovYPropagatesToCamera)
{
    CameraComponent cc;
    cc.SetFovY(NS::Math::Radians{1.0f});
    GameObject obj;
    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>(&obj.Root());
    follow.SetCamera(&cc);

    follow.SetFovY(NS::Math::Radians{0.5f});
    EXPECT_FLOAT_EQ(follow.FovY().value, 0.5f);
    EXPECT_FLOAT_EQ(cc.FovY().value, 0.5f);
}

TEST_F(ThirdPersonFollowTest, SensitivityAndInvertSettersPersist)
{
    ThirdPersonFollowComponent follow(nullptr);
    follow.SetSensX(0.01f);
    follow.SetSensY(0.02f);
    follow.SetInvertX(true);
    follow.SetInvertY(true);

    EXPECT_FLOAT_EQ(follow.SensX(), 0.01f);
    EXPECT_FLOAT_EQ(follow.SensY(), 0.02f);
    EXPECT_TRUE(follow.IsInvertX());
    EXPECT_TRUE(follow.IsInvertY());
}

TEST_F(ThirdPersonFollowTest, SetDistanceSyncsCurrentAndDesired)
{
    ThirdPersonFollowComponent follow(nullptr);
    follow.SetDistance(8.0f);
    EXPECT_FLOAT_EQ(follow.Distance(), 8.0f);
}

TEST_F(ThirdPersonFollowTest, PitchIsClampedAfterUpdate)
{
    GameObject obj;
    CameraComponent cc;
    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>(&obj.Root());
    follow.SetCamera(&cc);

    for (int i = 0; i < 200; ++i)
        follow.OnUpdate();

    EXPECT_GE(follow.Pitch(), -1.4f);
    EXPECT_LE(follow.Pitch(), 0.0f);
}
