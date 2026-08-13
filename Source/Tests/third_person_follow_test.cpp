#include <Runtime/Core/Clock.h>
#include <Runtime/Object/Components/ThirdPersonFollowComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Object::GameObject;
    using NS::Object::ThirdPersonFollowComponent;

    constexpr float k_Dt = 1.0f / 60.0f;
} // namespace

class ThirdPersonFollowTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_Dt); }
};

TEST_F(ThirdPersonFollowTest, ConstructsWithNullTarget)
{
    ThirdPersonFollowComponent follow;
    EXPECT_EQ(follow.Target(), nullptr);
    // 生成直後の休止は仕様。有効化はプレイ開始側が行う
    EXPECT_FALSE(follow.IsActive());
}

TEST_F(ThirdPersonFollowTest, OnUpdateRunsWhenActive)
{
    GameObject obj;
    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>();
    follow.SetTarget(&obj.Root());
    follow.OnUpdate();
    SUCCEED();
}

TEST_F(ThirdPersonFollowTest, EvaluatePoseFallbackWhenTargetIsNull)
{
    ThirdPersonFollowComponent follow;
    follow.OnUpdate();
    // target が無い時は EvaluatePose が既定 pose を返す。実カメラは動かない
    const auto pose = follow.EvaluatePose(1.0f);
    EXPECT_FLOAT_EQ(pose.position.z, -5.0f);
}

TEST_F(ThirdPersonFollowTest, EvaluatePosePlacesCameraBehindTarget)
{
    GameObject obj;
    obj.Root().SetPosition({0.0f, 0.0f, 0.0f});

    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>();
    follow.SetTarget(&obj.Root());
    follow.SetDistance(5.0f);

    for (int i = 0; i < 60; ++i)
        follow.OnUpdate();

    // OnUpdate は state mutation のみ (yaw/pitch/distance)、 最終姿勢は EvaluatePose が返す。揺れを避ける
    const auto pose = follow.EvaluatePose(1.0f);

    EXPECT_NEAR(pose.position.x, 0.0f, 0.1f);
    EXPECT_GT(pose.position.y, 1.0f);
    EXPECT_LT(pose.position.z, -3.0f);
    EXPECT_NEAR(pose.target.y, 1.2f, 0.01f);
}

TEST_F(ThirdPersonFollowTest, SensitivityAndInvertSettersPersist)
{
    ThirdPersonFollowComponent follow;
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
    ThirdPersonFollowComponent follow;
    follow.SetDistance(8.0f);
    EXPECT_FLOAT_EQ(follow.Distance(), 8.0f);
}

TEST_F(ThirdPersonFollowTest, PitchIsClampedAfterUpdate)
{
    GameObject obj;
    auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>();
    follow.SetTarget(&obj.Root());

    for (int i = 0; i < 200; ++i)
        follow.OnUpdate();

    EXPECT_GE(follow.Pitch(), -1.4f);
    EXPECT_LE(follow.Pitch(), 0.0f);
}
