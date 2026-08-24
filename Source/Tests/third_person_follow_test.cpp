#include <Runtime/Core/Clock.h>
#include <Runtime/Object/Components/ThirdPersonFollowComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <gtest/gtest.h>

#include <limits>

namespace
{
    using NS::Object::GameObject;
    using NS::Object::ThirdPersonFollowComponent;

    constexpr float k_Dt = 1.0f / 60.0f;

    constexpr float k_IdleDistance = 3.0f;
    constexpr float k_RunDistance = 8.0f;
    constexpr float k_JumpDistance = 12.0f;
    constexpr float k_RunSpeedThreshold = 4.0f;

    //! 3 段の距離を既定値から離して置く。どの段に寄ったかを距離 1 つで見分けられる
    ThirdPersonFollowComponent& MakeZoomProbe(GameObject& obj)
    {
        auto& follow = *obj.AddComponent<ThirdPersonFollowComponent>();
        follow.SetTarget(&obj.Root());
        follow.SetActive(true);
        follow.SetAutoDistances(k_IdleDistance, k_RunDistance, k_JumpDistance);
        follow.SetRunSpeedThreshold(k_RunSpeedThreshold);
        return follow;
    }

    //! 距離のばねが狙いへ収まるまで回す。観測できるのは現在距離だけで目標距離は外へ出ていない
    void SettleZoom(ThirdPersonFollowComponent& follow)
    {
        for (int i = 0; i < 200; ++i)
            follow.OnUpdate();
    }
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

TEST_F(ThirdPersonFollowTest, FollowMotionAirborneZoomsToJumpDistance)
{
    GameObject obj;
    auto& follow = MakeZoomProbe(obj);

    follow.SetFollowMotion(false, {0.0f, 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_JumpDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionAboveRunThresholdZoomsToRunDistance)
{
    GameObject obj;
    auto& follow = MakeZoomProbe(obj);

    follow.SetFollowMotion(true, {10.0f, 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_RunDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionStandingStillZoomsToIdleDistance)
{
    GameObject obj;
    auto& follow = MakeZoomProbe(obj);

    follow.SetFollowMotion(true, {0.0f, 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionRejectsNonFiniteVelocity)
{
    GameObject obj;
    auto& follow = MakeZoomProbe(obj);

    follow.SetFollowMotion(true, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, WithoutAnyFollowMotionTheDistanceStaysAtIdle)
{
    GameObject obj;
    auto& follow = MakeZoomProbe(obj);

    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}
