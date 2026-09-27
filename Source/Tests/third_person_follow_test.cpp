#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Object.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Mouse.h>
#include <gtest/gtest.h>

#include "camera_screen.h"
#include "tuning_field_access.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using NS::Obj::GameObject;
    using NS::Obj::Scene;
    using NS::Obj::ThirdPersonFollow;

    constexpr float k_Dt = 1.0f / 60.0f;

    constexpr float k_IdleDistance = 3.0f;
    constexpr float k_RunDistance = 8.0f;
    constexpr float k_JumpDistance = 12.0f;
    constexpr float k_RunSpeedThreshold = 4.0f;

    //! 原点に居る追従対象と、それを参照で追う追従カメラを scene へ置く
    ThirdPersonFollow& AddFollowing(Scene& scene)
    {
        // 参照で引く相手なので、id を振る SpawnObject で置く
        GameObject* target = scene.SpawnObject(std::make_unique<GameObject>(), "Target");
        GameObject* rig = scene.SpawnTransient<GameObject>();
        ThirdPersonFollow& follow = *rig->AddComponent<ThirdPersonFollow>();
        NsTest::WriteObjectRefField(follow, "追従対象", target->Id());
        return follow;
    }

    //! 3 段の距離を既定値から離して置く。どの段に寄ったかを距離 1 つで見分けられる
    ThirdPersonFollow& MakeZoomProbe(Scene& scene)
    {
        ThirdPersonFollow& follow = AddFollowing(scene);
        follow.SetActive(true);
        follow.SetAutoDistances(k_IdleDistance, k_RunDistance, k_JumpDistance);
        follow.SetRunSpeedThreshold(k_RunSpeedThreshold);
        return follow;
    }

    //! 距離のばねが狙いへ収まるまで回す。観測できるのは現在距離だけで目標距離は外へ出ていない
    void SettleZoom(ThirdPersonFollow& follow)
    {
        for (int i = 0; i < 200; ++i)
            follow.OnUpdate();
    }

    using NS::Core::Vector2;
    using NS::Core::Vector3;
    using NS::Obj::CameraPose;
    using NS::Obj::FollowChargeDesc;
    using NsTest::CameraRight;
    using NsTest::CameraUp;
    using NsTest::ScreenOf;

    // 溜めの構図の試しで使う既定の値
    constexpr float k_ChargeDistance = 5.0f;
    constexpr float k_NarrowMaxDegrees = 15.0f;
    constexpr int k_NarrowReturnFrames = 6;
    constexpr float k_ShakeStrength = 0.02f;
    constexpr float k_FrameRatio = 0.7f;

    //! 原点の相手を距離 5 m・既定のピッチ (下向き 15 度) で追う、動いている追従カメラ
    ThirdPersonFollow& MakeChargeProbe(Scene& scene)
    {
        ThirdPersonFollow& follow = AddFollowing(scene);
        follow.SetActive(true);
        follow.SetDistance(k_ChargeDistance);
        return follow;
    }

    [[nodiscard]] FollowChargeDesc HeldCharge(float charge01) noexcept
    {
        return FollowChargeDesc{.charge01 = charge01, .held = true};
    }

    [[nodiscard]] FollowChargeDesc HeldOnTarget(float charge01, const Vector3& center) noexcept
    {
        return FollowChargeDesc{.charge01 = charge01, .held = true, .hasAimTarget = true, .aimTargetCenter = center};
    }

    [[nodiscard]] FollowChargeDesc HeldOnBall(float charge01, const Vector3& center, float radius) noexcept
    {
        FollowChargeDesc desc = HeldOnTarget(charge01, center);
        desc.aimTargetRadius = radius;
        return desc;
    }

    void ExpectVectorNear(const Vector3& actual, const Vector3& expected, float tolerance, int frame)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance) << "frame " << frame;
        EXPECT_NEAR(actual.y, expected.y, tolerance) << "frame " << frame;
        EXPECT_NEAR(actual.z, expected.z, tolerance) << "frame " << frame;
    }

    // 滑らかに始まって終わる補間の重み
    [[nodiscard]] float SmoothWeight(float t) noexcept
    {
        return t * t * (3.0f - 2.0f * t);
    }
} // namespace

class ThirdPersonFollowTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Platform::FrameTimer::SetFixedDelta(k_Dt); }
};

TEST_F(ThirdPersonFollowTest, ConstructsWithNullTarget)
{
    ThirdPersonFollow follow;
    EXPECT_EQ(follow.Target(), nullptr);
    // 生成直後の休止は仕様。有効化はプレイ開始側が行う
    EXPECT_FALSE(follow.IsActive());
}

TEST_F(ThirdPersonFollowTest, OnUpdateRunsWhenActive)
{
    Scene scene;
    ThirdPersonFollow& follow = AddFollowing(scene);
    follow.OnUpdate();
    SUCCEED();
}

TEST_F(ThirdPersonFollowTest, EvaluatePoseFallbackWhenTargetIsNull)
{
    ThirdPersonFollow follow;
    follow.OnUpdate();
    // target が無い時は EvaluatePose が既定 pose を返す。実カメラは動かない
    const NS::Obj::CameraPose pose = follow.EvaluatePose(1.0f);
    EXPECT_FLOAT_EQ(pose.position.z, -5.0f);
}

TEST_F(ThirdPersonFollowTest, EvaluatePosePlacesCameraBehindTarget)
{
    Scene scene;
    ThirdPersonFollow& follow = AddFollowing(scene);
    follow.SetDistance(5.0f);

    for (int i = 0; i < 60; ++i)
        follow.OnUpdate();

    // OnUpdate は state mutation のみ (yaw/pitch/distance)、最終姿勢は EvaluatePose が返す。揺れを避ける
    const NS::Obj::CameraPose pose = follow.EvaluatePose(1.0f);

    EXPECT_NEAR(pose.position.x, 0.0f, 0.1f);
    EXPECT_GT(pose.position.y, 1.0f);
    EXPECT_LT(pose.position.z, -3.0f);
    EXPECT_NEAR(pose.target.y, 1.2f, 0.01f);
}

// 注視点は追う相手の根に、頭の高さと渡された縦のずれを足した所
TEST_F(ThirdPersonFollowTest, TargetHeightOffsetRaisesTheLookTarget)
{
    Scene scene;
    ThirdPersonFollow& follow = AddFollowing(scene);

    follow.SetTargetHeightOffset(0.5f);

    EXPECT_NEAR(follow.EvaluatePose(1.0f).target.y, 1.2f + 0.5f, 1e-5f);
}

TEST_F(ThirdPersonFollowTest, SensitivityAndInvertSettersPersist)
{
    ThirdPersonFollow follow;
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
    ThirdPersonFollow follow;
    follow.SetDistance(8.0f);
    EXPECT_FLOAT_EQ(follow.Distance(), 8.0f);
}

TEST_F(ThirdPersonFollowTest, PitchIsClampedAfterUpdate)
{
    Scene scene;
    ThirdPersonFollow& follow = AddFollowing(scene);

    for (int i = 0; i < 200; ++i)
        follow.OnUpdate();

    EXPECT_GE(follow.Pitch(), -1.4f);
    EXPECT_LE(follow.Pitch(), 0.0f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionAirborneZoomsToJumpDistance)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeZoomProbe(scene);

    follow.SetFollowMotion(false, {0.0f, 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_JumpDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionAboveRunThresholdZoomsToRunDistance)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeZoomProbe(scene);

    follow.SetFollowMotion(true, {10.0f, 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_RunDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionStandingStillZoomsToIdleDistance)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeZoomProbe(scene);

    follow.SetFollowMotion(true, {0.0f, 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, FollowMotionRejectsNonFiniteVelocity)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeZoomProbe(scene);

    follow.SetFollowMotion(true, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(ThirdPersonFollowTest, WithoutAnyFollowMotionTheDistanceStaysAtIdle)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeZoomProbe(scene);

    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

// 溜めを受けない時と、溜め量 0・押していない・相手なしを受けた時は、今の追従の式とビット単位で同じ姿勢
TEST_F(ThirdPersonFollowTest, UnchargedPoseMatchesTheFollowFormulaBitForBit)
{
    Scene scene;
    ThirdPersonFollow& plain = MakeChargeProbe(scene);
    ThirdPersonFollow& zeroed = MakeChargeProbe(scene);

    for (int i = 0; i < 30; ++i)
    {
        ASSERT_TRUE(zeroed.SetFollowCharge(FollowChargeDesc{}));
        plain.OnUpdate();
        zeroed.OnUpdate();
    }

    for (ThirdPersonFollow* follow : {&plain, &zeroed})
    {
        const float cy = std::cos(follow->Yaw());
        const float sy = std::sin(follow->Yaw());
        const float cp = std::cos(follow->Pitch());
        const float sp = std::sin(follow->Pitch());
        const Vector3 forward{sy * cp, sp, cy * cp};
        const Vector3 root = follow->Target()->InterpolatedWorldMatrix(1.0f).Translation();
        const Vector3 head{root.x, root.y + 0.0f + 1.2f, root.z};
        const float distance = follow->Distance();

        const CameraPose pose = follow->EvaluatePose(1.0f);
        EXPECT_EQ(pose.position.x, head.x - forward.x * distance);
        EXPECT_EQ(pose.position.y, head.y - forward.y * distance);
        EXPECT_EQ(pose.position.z, head.z - forward.z * distance);
        EXPECT_EQ(pose.target.x, head.x);
        EXPECT_EQ(pose.target.y, head.y);
        EXPECT_EQ(pose.target.z, head.z);
        EXPECT_EQ(pose.fovY.value, follow->FovY().value);
    }
}

// 締めは押している間の溜め量に比例し、視野角は基準から締めを引いた値。基準は書き換えない
TEST_F(ThirdPersonFollowTest, ChargeNarrowsTheViewInProportionWhileHeld)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const float baseDegrees = NS::Core::RadiansToDegrees(follow.FovY().value);

    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(0.5f)));
    follow.OnUpdate();
    EXPECT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees * 0.5f, 1e-5f);
    EXPECT_NEAR(NS::Core::RadiansToDegrees(follow.EvaluatePose(1.0f).fovY.value), baseDegrees - 7.5f, 1e-4f);

    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(1.0f)));
    follow.OnUpdate();
    EXPECT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees, 1e-5f);
    EXPECT_NEAR(NS::Core::RadiansToDegrees(follow.EvaluatePose(1.0f).fovY.value), baseDegrees - 15.0f, 1e-4f);
    EXPECT_FLOAT_EQ(NS::Core::RadiansToDegrees(follow.FovY().value), baseDegrees);
}

// 放したフレームから 6 フレームで滑らかに 0 へ戻り、6 フレーム目で 0 ちょうど。溜め量が残っていても押していなければ戻る
// 戻しの途中で押し直すと、戻しの値と新しい溜めの締めの大きい方
TEST_F(ThirdPersonFollowTest, NarrowEasesBackAfterReleaseAndTakesTheLargerOnRepress)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(1.0f)));
    follow.OnUpdate();
    ASSERT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees, 1e-5f);

    const FollowChargeDesc released{.charge01 = 1.0f, .held = false};
    for (int frame = 1; frame <= k_NarrowReturnFrames; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(released));
        follow.OnUpdate();
        const float t = static_cast<float>(frame) / static_cast<float>(k_NarrowReturnFrames);
        EXPECT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees * (1.0f - SmoothWeight(t)), 1e-4f)
            << "frame " << frame;
    }
    EXPECT_EQ(follow.ChargeNarrowDegrees(), 0.0f);

    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(1.0f)));
    follow.OnUpdate();
    for (int frame = 1; frame <= 2; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(released));
        follow.OnUpdate();
    }

    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(0.1f)));
    follow.OnUpdate();
    EXPECT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees * (1.0f - SmoothWeight(3.0f / 6.0f)), 1e-4f);

    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(0.9f)));
    follow.OnUpdate();
    EXPECT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees * 0.9f, 1e-4f);
}

// 溜めの揺れは位置と注視点を同じだけカメラの上の向きに動かし、毎フレーム向きが入れ替わり、最初は下
// 振れ幅は 0.02 × 溜め量で、押していない値を受けたフレームで 0
TEST_F(ThirdPersonFollowTest, ChargeShakeFlipsVerticallyEveryFrameStartingDown)
{
    Scene scene;
    ThirdPersonFollow& base = MakeChargeProbe(scene);
    ThirdPersonFollow& shaken = MakeChargeProbe(scene);
    constexpr float charge = 0.5f;
    const float amplitude = k_ShakeStrength * charge;

    for (int frame = 0; frame < 4; ++frame)
    {
        ASSERT_TRUE(shaken.SetFollowCharge(HeldCharge(charge)));
        base.OnUpdate();
        shaken.OnUpdate();
        float expected = -amplitude;
        if (frame % 2 == 1)
        {
            expected = amplitude;
        }
        EXPECT_NEAR(shaken.ChargeShake(), expected, 1e-6f) << "frame " << frame;
        const CameraPose basePose = base.EvaluatePose(1.0f);
        const CameraPose shakenPose = shaken.EvaluatePose(1.0f);
        const Vector3 up = CameraUp(shaken);
        ExpectVectorNear(shakenPose.position - basePose.position, up * expected, 1e-5f, frame);
        ExpectVectorNear(shakenPose.target - basePose.target, up * expected, 1e-5f, frame);
    }

    ASSERT_TRUE(shaken.SetFollowCharge(FollowChargeDesc{.charge01 = charge, .held = false}));
    shaken.OnUpdate();
    EXPECT_EQ(shaken.ChargeShake(), 0.0f);
}

// 真横 6 m の相手は押してから 11 フレームで要るずらしの 95% に届き、収まった後は相手が枠 0.7 の端、自機も枠の内
TEST_F(ThirdPersonFollowTest, FramingBringsASideTargetIntoTheFrameBeforeTheChargeBegins)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const Vector3 self{0.0f, 0.0f, 0.0f};
    const Vector3 side{6.0f, 0.0f, 0.0f};
    ASSERT_GT(ScreenOf(follow.EvaluatePose(1.0f), side).x, 1.0f);

    for (int frame = 0; frame < 11; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(HeldOnTarget(0.0f, side)));
        follow.OnUpdate();
    }
    const float afterPressWindow = follow.ChargeFrameOffset().x;
    for (int frame = 0; frame < 120; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(HeldOnTarget(0.0f, side)));
        follow.OnUpdate();
    }
    const float settled = follow.ChargeFrameOffset().x;

    ASSERT_GT(settled, 0.0f);
    EXPECT_GE(afterPressWindow, 0.95f * settled);
    const CameraPose pose = follow.EvaluatePose(1.0f);
    EXPECT_NEAR(ScreenOf(pose, side).x, k_FrameRatio, 1e-3f);
    EXPECT_LE(std::abs(ScreenOf(pose, side).y), k_FrameRatio);
    EXPECT_LE(std::abs(ScreenOf(pose, self).x), k_FrameRatio);
    EXPECT_LE(std::abs(ScreenOf(pose, self).y), k_FrameRatio);
}

// 自機と相手が両方とも枠の内なら、構図は動かない
TEST_F(ThirdPersonFollowTest, FramingStaysStillWhenBothAreInside)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const Vector3 ahead{1.0f, 0.0f, 3.0f};
    ASSERT_LE(std::abs(ScreenOf(follow.EvaluatePose(1.0f), ahead).x), k_FrameRatio);
    ASSERT_LE(std::abs(ScreenOf(follow.EvaluatePose(1.0f), ahead).y), k_FrameRatio);

    for (int frame = 0; frame < 30; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(HeldOnTarget(1.0f, ahead)));
        follow.OnUpdate();
    }

    EXPECT_EQ(follow.ChargeFrameOffset().x, 0.0f);
    EXPECT_EQ(follow.ChargeFrameOffset().y, 0.0f);
}

// 真横 12 m の相手は自機と一緒には枠に入らない。自機を枠の内に残し、相手の側へ寄せる
TEST_F(ThirdPersonFollowTest, FramingKeepsTheSelfWhenBothCannotFit)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const Vector3 self{0.0f, 0.0f, 0.0f};
    const Vector3 farSide{12.0f, 0.0f, 0.0f};

    for (int frame = 0; frame < 120; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(HeldOnTarget(0.0f, farSide)));
        follow.OnUpdate();
    }

    const CameraPose pose = follow.EvaluatePose(1.0f);
    EXPECT_GT(follow.ChargeFrameOffset().x, 0.0f);
    EXPECT_NEAR(ScreenOf(pose, self).x, -k_FrameRatio, 1e-3f);
    EXPECT_GT(ScreenOf(pose, farSide).x, k_FrameRatio);
}

// 半径 0.75 m の相手は、中心でなく玉の縁が枠 0.7 の端に来るまで寄せる。真横の相手は右の縁、下の相手は下の縁
TEST_F(ThirdPersonFollowTest, FramingKeepsTheWholeTargetBallInsideTheFrame)
{
    constexpr float radius = 0.75f;
    Scene scene;
    ThirdPersonFollow& sideFollow = MakeChargeProbe(scene);
    ThirdPersonFollow& belowFollow = MakeChargeProbe(scene);
    const Vector3 self{0.0f, 0.0f, 0.0f};
    const Vector3 side{6.0f, 0.0f, 0.0f};
    const Vector3 below{0.0f, -4.0f, 2.0f};

    for (int frame = 0; frame < 120; ++frame)
    {
        ASSERT_TRUE(sideFollow.SetFollowCharge(HeldOnBall(0.0f, side, radius)));
        ASSERT_TRUE(belowFollow.SetFollowCharge(HeldOnBall(0.0f, below, radius)));
        sideFollow.OnUpdate();
        belowFollow.OnUpdate();
    }

    const CameraPose sidePose = sideFollow.EvaluatePose(1.0f);
    EXPECT_NEAR(ScreenOf(sidePose, side + CameraRight(sideFollow) * radius).x, k_FrameRatio, 1e-3f);
    EXPECT_LE(std::abs(ScreenOf(sidePose, self).x), k_FrameRatio);
    const CameraPose belowPose = belowFollow.EvaluatePose(1.0f);
    EXPECT_NEAR(ScreenOf(belowPose, below - CameraUp(belowFollow) * radius).y, -k_FrameRatio, 1e-3f);
    EXPECT_LE(std::abs(ScreenOf(belowPose, self).y), k_FrameRatio);
}

// 締め・溜めの揺れ・構図のずらしの間も、注視点 − 位置 の水平の向きは受ける前と同じ
TEST_F(ThirdPersonFollowTest, ChargeViewKeepsTheViewDirection)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const CameraPose before = follow.EvaluatePose(1.0f);
    Vector3 forwardBefore{};
    ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(before.target - before.position, forwardBefore));

    for (int frame = 0; frame < 20; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowCharge(HeldOnTarget(1.0f, Vector3{6.0f, 0.0f, 0.0f})));
        follow.OnUpdate();
        const CameraPose pose = follow.EvaluatePose(1.0f);
        Vector3 forward{};
        ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(pose.target - pose.position, forward));
        EXPECT_NEAR(forward.x, forwardBefore.x, 1e-6f) << "frame " << frame;
        EXPECT_NEAR(forward.z, forwardBefore.z, 1e-6f) << "frame " << frame;
    }
    ASSERT_NE(follow.ChargeFrameOffset().x, 0.0f);
    ASSERT_NE(follow.ChargeShake(), 0.0f);
}

// 読み口の締め・揺れ・ずらしは、溜めを受けていない姿勢へ足した量そのもの
TEST_F(ThirdPersonFollowTest, ChargeReadoutsMatchWhatThePoseAdds)
{
    Scene scene;
    ThirdPersonFollow& base = MakeChargeProbe(scene);
    ThirdPersonFollow& charged = MakeChargeProbe(scene);

    for (int frame = 0; frame < 8; ++frame)
    {
        ASSERT_TRUE(charged.SetFollowCharge(HeldOnTarget(0.5f, Vector3{6.0f, 0.0f, 0.0f})));
        base.OnUpdate();
        charged.OnUpdate();
    }
    ASSERT_NE(charged.ChargeFrameOffset().x, 0.0f);
    ASSERT_NE(charged.ChargeShake(), 0.0f);

    const CameraPose basePose = base.EvaluatePose(1.0f);
    const CameraPose chargedPose = charged.EvaluatePose(1.0f);
    const Vector2 offset = charged.ChargeFrameOffset();
    const Vector3 added = CameraRight(charged) * offset.x + CameraUp(charged) * (offset.y + charged.ChargeShake());
    ExpectVectorNear(chargedPose.position - basePose.position, added, 1e-4f, 8);
    ExpectVectorNear(chargedPose.target - basePose.target, added, 1e-4f, 8);
    EXPECT_NEAR(
        basePose.fovY.value - chargedPose.fovY.value, NS::Core::DegreesToRadians(charged.ChargeNarrowDegrees()), 1e-6f);
}

// 0 にする関数の後は締め・揺れ・ずらしが 0 で、受けていた溜めも捨てるので、次のフレームも溜めを受けていない姿勢
TEST_F(ThirdPersonFollowTest, ClearChargeReturnsToTheUnchargedPose)
{
    Scene scene;
    ThirdPersonFollow& base = MakeChargeProbe(scene);
    ThirdPersonFollow& charged = MakeChargeProbe(scene);
    for (int frame = 0; frame < 8; ++frame)
    {
        ASSERT_TRUE(charged.SetFollowCharge(HeldOnTarget(0.5f, Vector3{6.0f, 0.0f, 0.0f})));
        base.OnUpdate();
        charged.OnUpdate();
    }
    ASSERT_NE(charged.ChargeNarrowDegrees(), 0.0f);

    ASSERT_TRUE(charged.SetFollowCharge(HeldOnTarget(0.5f, Vector3{6.0f, 0.0f, 0.0f})));
    charged.ClearCharge();
    EXPECT_EQ(charged.ChargeNarrowDegrees(), 0.0f);
    EXPECT_EQ(charged.ChargeShake(), 0.0f);
    EXPECT_EQ(charged.ChargeFrameOffset().x, 0.0f);
    EXPECT_EQ(charged.ChargeFrameOffset().y, 0.0f);

    base.OnUpdate();
    charged.OnUpdate();
    const CameraPose basePose = base.EvaluatePose(1.0f);
    const CameraPose chargedPose = charged.EvaluatePose(1.0f);
    EXPECT_EQ(chargedPose.position.x, basePose.position.x);
    EXPECT_EQ(chargedPose.position.y, basePose.position.y);
    EXPECT_EQ(chargedPose.position.z, basePose.position.z);
    EXPECT_EQ(chargedPose.target.x, basePose.target.x);
    EXPECT_EQ(chargedPose.target.y, basePose.target.y);
    EXPECT_EQ(chargedPose.target.z, basePose.target.z);
    EXPECT_EQ(chargedPose.fovY.value, basePose.fovY.value);
}

// 壊れた溜めは偽を返し、先に受けていた溜めを上書きしない
TEST_F(ThirdPersonFollowTest, BrokenChargeIsRejectedAndKeepsTheReceivedOne)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(follow.SetFollowCharge(HeldCharge(0.5f)));

    EXPECT_FALSE(follow.SetFollowCharge(HeldCharge(nan)));
    EXPECT_FALSE(follow.SetFollowCharge(HeldCharge(-0.1f)));
    EXPECT_FALSE(follow.SetFollowCharge(HeldCharge(1.1f)));
    EXPECT_FALSE(follow.SetFollowCharge(HeldOnTarget(0.5f, Vector3{nan, 0.0f, 0.0f})));
    EXPECT_FALSE(follow.SetFollowCharge(HeldOnBall(0.5f, Vector3{6.0f, 0.0f, 0.0f}, nan)));
    EXPECT_FALSE(follow.SetFollowCharge(HeldOnBall(0.5f, Vector3{6.0f, 0.0f, 0.0f}, -0.1f)));
    follow.OnUpdate();

    EXPECT_NEAR(follow.ChargeNarrowDegrees(), k_NarrowMaxDegrees * 0.5f, 1e-5f);
    EXPECT_EQ(follow.ChargeFrameOffset().x, 0.0f);
}

namespace
{
    // 反動の間のカメラの試しで使う既定の値
    constexpr float k_ReboundMaxLag = 1.5f;
    constexpr float k_ReboundScreenBand = 0.5f;
    constexpr int k_ReboundReturnFrames = 20;
    constexpr int k_ReboundTurnFrames = 20;
    constexpr float k_ReboundPullBack = 1.0f;
    constexpr float k_HeadHeight = 1.2f;
    constexpr float k_SensX = 0.003f; // 既定のマウス水平感度 (ラジアン / 画素)

    using NS::Core::k_Pi;
    using NS::Obj::FollowReboundDesc;

    //! 追う相手を position へ動かし、接地と反動の状態を渡して 1 フレーム進める
    //! 前フレームの位置は Scene::OnUpdate の頭と同じく動かす前に控える
    //! slamDirection は反動を起こした突進を出した向き。
    //! mouseDx と mouseDy はこのフレームにマウスを動かす画素数で、進めた後に差を 0 へ戻す
    void StepFollow(ThirdPersonFollow& follow,
                    const Vector3& position,
                    bool rebounding,
                    bool grounded,
                    const Vector3& slamDirection = Vector3{0.0f, 0.0f, 0.0f},
                    int mouseDx = 0,
                    int mouseDy = 0)
    {
        NS::Obj::Transform& target = *follow.Target();
        target.Snapshot();
        target.SetPosition(position);
        follow.SetFollowMotion(grounded, Vector3{0.0f, 0.0f, 0.0f});
        EXPECT_TRUE(
            follow.SetFollowRebound(FollowReboundDesc{.rebounding = rebounding, .slamDirection = slamDirection}));
        NS::Platform::Mouse& mouse = NS::Platform::Input::Get().Mouse();
        mouse.Update();
        if (mouse.IsRelativeMode())
        {
            mouse.OnRawMove(mouseDx, mouseDy);
        }
        else
        {
            mouse.OnMove(mouse.GetX() + mouseDx, mouse.GetY() + mouseDy);
        }
        follow.OnUpdate();
        mouse.Update();
    }

    //! 今の追う相手の位置から、反動を受けない時の追従の式で注視点を出す
    [[nodiscard]] Vector3 PlainLookTarget(const ThirdPersonFollow& follow, float alpha)
    {
        const Vector3 root = follow.Target()->InterpolatedWorldMatrix(alpha).Translation();
        return Vector3{root.x, root.y + k_HeadHeight, root.z};
    }

    //! 注視点が普通の追い方の注視点からどれだけ離れているか (m)
    [[nodiscard]] float LookGap(const ThirdPersonFollow& follow)
    {
        return (follow.EvaluatePose(1.0f).target - PlainLookTarget(follow, 1.0f)).Length();
    }
} // namespace

// 反動の間は、自機が上がっても注視点の高さは当たった瞬間のまま
// 描画の補間の途中でも動かない
TEST_F(ThirdPersonFollowTest, ReboundHoldsTheLookHeightOfTheHitFrame)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    for (int frame = 0; frame < 3; ++frame)
    {
        StepFollow(follow, Vector3{0.0f, 0.0f, 0.0f}, false, true);
    }
    const float hitLookY = follow.EvaluatePose(1.0f).target.y;

    for (int frame = 1; frame <= 40; ++frame)
    {
        const float rise = 1.5f * std::sin(3.14159265f * static_cast<float>(frame) / 40.0f);
        StepFollow(follow, Vector3{0.0f, rise, 0.0f}, true, false);
        EXPECT_NEAR(follow.EvaluatePose(1.0f).target.y, hitLookY, 1e-5f) << "frame " << frame;
        EXPECT_NEAR(follow.EvaluatePose(0.5f).target.y, hitLookY, 1e-5f) << "frame " << frame;
    }
}

// 反動の間、横と前後は普通の追い方より遅れて付いていき、遅れは欄の上限を超えない
TEST_F(ThirdPersonFollowTest, ReboundFollowsSidewaysAndBackBehindTheBall)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const Vector3 direction{0.70710678f, 0.0f, -0.70710678f};
    StepFollow(follow, Vector3{0.0f, 0.0f, 0.0f}, false, true);
    float previousLookX = follow.EvaluatePose(1.0f).target.x;

    for (int frame = 1; frame <= 60; ++frame)
    {
        const Vector3 root = direction * (4.0f * k_Dt * static_cast<float>(frame));
        StepFollow(follow, root, true, false);
        const Vector3 look = follow.EvaluatePose(1.0f).target;
        const Vector3 lag{root.x - look.x, 0.0f, root.z - look.z};
        EXPECT_GT(look.x, previousLookX) << "frame " << frame;
        // 描画の補間の始まりは前のフレームの注視点
        // 固定ステップの間で注視点が跳ばない
        EXPECT_NEAR(follow.EvaluatePose(0.0f).target.x, previousLookX, 1e-5f) << "frame " << frame;
        EXPECT_GT(NS::Core::Dot(lag, direction), 0.0f) << "frame " << frame;
        EXPECT_LE(lag.Length(), k_ReboundMaxLag + 1e-4f) << "frame " << frame;
        previousLookX = look.x;
    }
    EXPECT_GT(LookGap(follow), 0.5f);
}

// 反動の間に自機が画面の上か下の帯を越えそうになると、越えない所まで追う
TEST_F(ThirdPersonFollowTest, ReboundTracksTheBallAtTheScreenBand)
{
    Scene scene;
    ThirdPersonFollow& rising = MakeChargeProbe(scene);
    ThirdPersonFollow& falling = MakeChargeProbe(scene);

    for (int frame = 1; frame <= 60; ++frame)
    {
        const float height = 6.0f * static_cast<float>(frame) / 60.0f;
        StepFollow(rising, Vector3{0.0f, height, 0.0f}, true, false);
        StepFollow(falling, Vector3{0.0f, -height, 0.0f}, true, false);
        EXPECT_LE(ScreenOf(rising.EvaluatePose(1.0f), rising.Target()->Position()).y, k_ReboundScreenBand + 1e-3f)
            << "frame " << frame;
        EXPECT_GE(ScreenOf(falling.EvaluatePose(1.0f), falling.Target()->Position()).y, -k_ReboundScreenBand - 1e-3f)
            << "frame " << frame;
    }
    EXPECT_NEAR(ScreenOf(rising.EvaluatePose(1.0f), rising.Target()->Position()).y, k_ReboundScreenBand, 1e-3f);
    EXPECT_NEAR(ScreenOf(falling.EvaluatePose(1.0f), falling.Target()->Position()).y, -k_ReboundScreenBand, 1e-3f);
}

// 反動の状態が外れたら欄のフレーム数で普通の追い方へ寄せ、
// 最後のフレームで普通の式とビット単位で同じ姿勢になる
// 空中の 1 発で外れた時も着地を待たないので、寄せ戻しの間は空中のまま渡す
TEST_F(ThirdPersonFollowTest, ReboundEndEasesBackToTheNormalFollowOverTheReturnFrames)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    for (int frame = 1; frame <= 40; ++frame)
    {
        const float t = static_cast<float>(frame) / 40.0f;
        StepFollow(follow, Vector3{2.0f * t, 1.0f * std::sin(3.14159265f * t), 0.0f}, true, false);
    }
    const Vector3 released{2.0f, 0.0f, 0.0f};
    StepFollow(follow, released, true, false);
    const float heldGap = LookGap(follow);
    ASSERT_GT(heldGap, 0.3f);

    float previousGap = heldGap;
    for (int frame = 1; frame < k_ReboundReturnFrames; ++frame)
    {
        StepFollow(follow, released, false, false);
        const float gap = LookGap(follow);
        EXPECT_GT(gap, 0.0f) << "frame " << frame;
        EXPECT_LT(gap, previousGap) << "frame " << frame;
        if (frame == 1)
        {
            EXPECT_GT(gap, 0.95f * heldGap);
        }
        previousGap = gap;
    }
    StepFollow(follow, released, false, false);

    const float cy = std::cos(follow.Yaw());
    const float sy = std::sin(follow.Yaw());
    const float cp = std::cos(follow.Pitch());
    const float sp = std::sin(follow.Pitch());
    const Vector3 forward{sy * cp, sp, cy * cp};
    const Vector3 head = PlainLookTarget(follow, 1.0f);
    const CameraPose pose = follow.EvaluatePose(1.0f);
    EXPECT_EQ(pose.target.x, head.x);
    EXPECT_EQ(pose.target.y, head.y);
    EXPECT_EQ(pose.target.z, head.z);
    EXPECT_EQ(pose.position.x, head.x - forward.x * follow.Distance());
    EXPECT_EQ(pose.position.y, head.y - forward.y * follow.Distance());
    EXPECT_EQ(pose.position.z, head.z - forward.z * follow.Distance());
}

// 反動でない空中 (普通の跳び) は、上下も横も追う相手の頭をそのまま見る
TEST_F(ThirdPersonFollowTest, PlainJumpKeepsFollowingTheHead)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);

    for (int frame = 1; frame <= 40; ++frame)
    {
        const float t = static_cast<float>(frame) / 40.0f;
        StepFollow(follow, Vector3{2.0f * t, 2.0f * std::sin(3.14159265f * t), 0.0f}, false, false);
        const Vector3 look = follow.EvaluatePose(0.5f).target;
        const Vector3 head = PlainLookTarget(follow, 0.5f);
        EXPECT_EQ(look.x, head.x) << "frame " << frame;
        EXPECT_EQ(look.y, head.y) << "frame " << frame;
        EXPECT_EQ(look.z, head.z) << "frame " << frame;
    }
}

// 反動の間は空中でもジャンプ時距離へ引かず、
// 当たった瞬間の距離より欄の分だけ後ろへ下がる
TEST_F(ThirdPersonFollowTest, ReboundPullsBackFromTheDistanceOfTheHitFrame)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeZoomProbe(scene);
    for (int frame = 0; frame < 200; ++frame)
    {
        StepFollow(follow, Vector3{0.0f, 0.0f, 0.0f}, false, true);
    }
    ASSERT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);

    float previous = follow.Distance();
    for (int frame = 1; frame <= 60; ++frame)
    {
        StepFollow(follow, Vector3{0.0f, 0.5f, 0.0f}, true, false);
        EXPECT_GT(follow.Distance(), previous) << "frame " << frame;
        EXPECT_LT(follow.Distance(), k_IdleDistance + k_ReboundPullBack + 1e-4f) << "frame " << frame;
        previous = follow.Distance();
    }
    EXPECT_NEAR(follow.Distance(), k_IdleDistance + k_ReboundPullBack, 0.01f);
}

// 反動になったフレームから欄のフレーム数で、水平の向きを突進を出した向きへ回し、
// 最後のフレームで揃う
// 縦の角度は変えない。揃った後は反動の間も動かない
TEST_F(ThirdPersonFollowTest, ReboundTurnsTheYawToTheSlamDirectionOverTheTurnFrames)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    StepFollow(follow, Vector3{0.0f, 0.0f, 0.0f}, false, true);
    ASSERT_EQ(follow.Yaw(), 0.0f);
    const float pitch = follow.Pitch();

    // 縦の成分と長さは使わない。-X を向くので、水平の向きは -90 度
    const Vector3 slam{-2.0f, 0.5f, 0.0f};
    float previous = follow.Yaw();
    for (int frame = 1; frame <= k_ReboundTurnFrames; ++frame)
    {
        StepFollow(follow, Vector3{0.05f * static_cast<float>(frame), 0.5f, 0.0f}, true, false, slam);
        EXPECT_LT(follow.Yaw(), previous) << "frame " << frame;
        EXPECT_EQ(follow.Pitch(), pitch) << "frame " << frame;
        if (frame < k_ReboundTurnFrames)
        {
            EXPECT_GT(follow.Yaw(), -0.5f * k_Pi + 1e-3f) << "frame " << frame;
        }
        previous = follow.Yaw();
    }
    EXPECT_NEAR(follow.Yaw(), -0.5f * k_Pi, 1e-5f);

    StepFollow(follow, Vector3{1.1f, 0.5f, 0.0f}, true, false, slam);
    EXPECT_NEAR(follow.Yaw(), -0.5f * k_Pi, 1e-5f);
}

// 向きの差が半周を越える時は、逆の側から近い方へ回る
TEST_F(ThirdPersonFollowTest, ReboundTurnsTheShortWayAround)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    // 水平の向き 3.0 から見る所へカメラを置く
    const Vector3 head{0.0f, k_HeadHeight, 0.0f};
    follow.SetInitialPoseFromCameraPosition(head - Vector3{std::sin(3.0f), 0.0f, std::cos(3.0f)} * 5.0f);
    const float start = follow.Yaw();
    ASSERT_NEAR(start, 3.0f, 1e-4f);

    // 水平の向き -3.0 は、3.0 から +側へ 2π − 6 回った所
    const Vector3 slam{std::sin(-3.0f), 0.0f, std::cos(-3.0f)};
    for (int frame = 1; frame <= k_ReboundTurnFrames; ++frame)
    {
        StepFollow(follow, Vector3{0.0f, 0.5f, 0.0f}, true, false, slam);
        EXPECT_GE(follow.Yaw(), start) << "frame " << frame;
    }
    EXPECT_NEAR(follow.Yaw(), 2.0f * k_Pi - 3.0f, 1e-4f);
}

// 反動になってから着地するまで、マウスで向きを回せない。
// 反動の状態が外れても空中の間は回せず、着地したフレームから回せる。
// 反動でない跳びは空中でも回せる
TEST_F(ThirdPersonFollowTest, LookInputIsIgnoredFromTheReboundUntilLanding)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const Vector3 air{0.0f, 1.0f, 0.0f};
    StepFollow(follow, air, false, false, Vector3{0.0f, 0.0f, 0.0f}, 100, 10);
    const float yaw = follow.Yaw();
    const float pitch = follow.Pitch();
    ASSERT_NEAR(yaw, 100.0f * k_SensX, 1e-5f);

    // 今の向きと同じ向きの突進で当てたので、揃える回りは無い
    const Vector3 slam{std::sin(yaw), 0.0f, std::cos(yaw)};
    for (int frame = 1; frame <= 30; ++frame)
    {
        StepFollow(follow, air, true, false, slam, 100, 10);
        EXPECT_NEAR(follow.Yaw(), yaw, 1e-5f) << "frame " << frame;
        EXPECT_EQ(follow.Pitch(), pitch) << "frame " << frame;
    }
    for (int frame = 1; frame <= 10; ++frame)
    {
        StepFollow(follow, air, false, false, Vector3{0.0f, 0.0f, 0.0f}, 100, 10);
        EXPECT_NEAR(follow.Yaw(), yaw, 1e-5f) << "air frame " << frame;
        EXPECT_EQ(follow.Pitch(), pitch) << "air frame " << frame;
    }

    StepFollow(follow, Vector3{0.0f, 0.0f, 0.0f}, false, true, Vector3{0.0f, 0.0f, 0.0f}, 100, 10);
    EXPECT_NEAR(follow.Yaw(), yaw + 100.0f * k_SensX, 1e-5f);
    EXPECT_NE(follow.Pitch(), pitch);
}

// 反動の状態が外れた空中で出した 1 発が当たって反動になり直したら、
// その 1 発を出した向きへ揃え直す
TEST_F(ThirdPersonFollowTest, AirShotReboundRealignsToItsSlamDirection)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const Vector3 air{0.0f, 1.0f, 0.0f};
    for (int frame = 1; frame <= k_ReboundTurnFrames; ++frame)
    {
        StepFollow(follow, air, true, false, Vector3{1.0f, 0.0f, 0.0f});
    }
    ASSERT_NEAR(follow.Yaw(), 0.5f * k_Pi, 1e-5f);
    for (int frame = 1; frame <= 5; ++frame)
    {
        StepFollow(follow, air, false, false);
    }

    for (int frame = 1; frame <= k_ReboundTurnFrames; ++frame)
    {
        StepFollow(follow, air, true, false, Vector3{0.0f, 0.0f, -1.0f});
    }
    EXPECT_NEAR(follow.Yaw(), k_Pi, 1e-5f);
}

// 壊れた反動の状態は受けず、先に受けた状態のまま次のフレームを進める
TEST_F(ThirdPersonFollowTest, BrokenReboundIsRejectedAndKeepsTheReceivedOne)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int frame = 1; frame <= k_ReboundTurnFrames; ++frame)
    {
        ASSERT_TRUE(follow.SetFollowRebound(
            FollowReboundDesc{.rebounding = true, .slamDirection = Vector3{1.0f, 0.0f, 0.0f}}));
        EXPECT_FALSE(follow.SetFollowRebound(
            FollowReboundDesc{.rebounding = false, .slamDirection = Vector3{nan, 0.0f, 0.0f}}));
        EXPECT_FALSE(follow.SetFollowRebound(
            FollowReboundDesc{.rebounding = false, .slamDirection = Vector3{0.0f, 0.0f, inf}}));
        follow.OnUpdate();
    }
    EXPECT_NEAR(follow.Yaw(), 0.5f * k_Pi, 1e-5f);
}

// 0 にする関数の後は、次のフレームを待たずに普通の追い方の姿勢
TEST_F(ThirdPersonFollowTest, ClearReboundReturnsToTheNormalFollowAtOnce)
{
    Scene scene;
    ThirdPersonFollow& follow = MakeChargeProbe(scene);
    for (int frame = 1; frame <= 30; ++frame)
    {
        StepFollow(follow, Vector3{0.05f * static_cast<float>(frame), 1.0f, 0.0f}, true, false);
    }
    ASSERT_GT(LookGap(follow), 0.3f);

    follow.ClearRebound();

    const Vector3 look = follow.EvaluatePose(1.0f).target;
    const Vector3 head = PlainLookTarget(follow, 1.0f);
    EXPECT_EQ(look.x, head.x);
    EXPECT_EQ(look.y, head.y);
    EXPECT_EQ(look.z, head.z);
}
