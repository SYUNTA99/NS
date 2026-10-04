#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

// トラウマの揺れ (外れと溜め) の決まりを縛る。連続・一撃・トラウマ・入力の向き・倍率

namespace
{
    NS::Obj::CameraTraumaShape ShapeOf(float frequency, float decayPerSecond)
    {
        NS::Obj::CameraTraumaShape shape;
        shape.yawDegrees = 4.0f;
        shape.pitchDegrees = 2.0f;
        shape.rollDegrees = 1.0f;
        shape.frequency = frequency;
        shape.decayPerSecond = decayPerSecond;
        shape.exponent = 2.0f;
        return shape;
    }

    NS::Obj::CameraTraumaDesc TraumaOf(float trauma)
    {
        NS::Obj::CameraTraumaDesc desc;
        desc.trauma = trauma;
        desc.shape = ShapeOf(10.0f, 1.0f);
        desc.seed = 5u;
        return desc;
    }

    float AngleLength(const NS::Core::Vector3& angles)
    {
        return std::sqrt(angles.x * angles.x + angles.y * angles.y + angles.z * angles.z);
    }
} // namespace

// 振れ幅はトラウマの 2 乗。0.5 で最大の 1/4
TEST(CameraTrauma, HalfTraumaShakesAQuarter)
{
    NS::Obj::CameraTraumaModifier trauma;
    trauma.AddTrauma(TraumaOf(0.5f));
    EXPECT_FLOAT_EQ(trauma.ShakeAmount(), 0.25f);
    for (int i = 0; i < 30; ++i)
    {
        trauma.Tick();
        const NS::Core::Vector3 angles = trauma.Angles();
        EXPECT_LE(std::fabs(angles.x), 4.0f * 0.25f + 1.0e-5f);
        EXPECT_LE(std::fabs(angles.y), 2.0f * 0.25f + 1.0e-5f);
        EXPECT_LE(std::fabs(angles.z), 1.0f * 0.25f + 1.0e-5f);
    }
}

// 続けて足すと足され、1 で頭打ち。時間で直線に減って終わる
TEST(CameraTrauma, AddsUpToOneThenDecaysLinearly)
{
    NS::Obj::CameraTraumaModifier trauma;
    trauma.AddTrauma(TraumaOf(0.6f));
    trauma.AddTrauma(TraumaOf(0.3f));
    EXPECT_FLOAT_EQ(trauma.Trauma(), 0.9f);
    trauma.AddTrauma(TraumaOf(0.6f));
    EXPECT_FLOAT_EQ(trauma.Trauma(), 1.0f);
    // 積んだ直後の Tick は進めない
    trauma.Tick();
    EXPECT_FLOAT_EQ(trauma.Trauma(), 1.0f);
    trauma.Tick();
    EXPECT_NEAR(trauma.Trauma(), 1.0f - 1.0f / 60.0f, 1.0e-5f);
    for (int i = 0; i < 60; ++i)
    {
        trauma.Tick();
    }
    EXPECT_FLOAT_EQ(trauma.Trauma(), 0.0f);
    EXPECT_TRUE(trauma.IsFinished());
}

// 隣り合うフレームの角度の差は、振れ幅に対して小さい (方形の波は約 2 倍を跳ぶ)
TEST(CameraTrauma, NeighbouringFramesDoNotJump)
{
    NS::Obj::CameraTraumaModifier trauma;
    NS::Obj::CameraTraumaDesc desc = TraumaOf(1.0f);
    desc.shape.decayPerSecond = 0.5f;
    trauma.AddTrauma(desc);
    trauma.Tick();
    NS::Core::Vector3 previous = trauma.Angles();
    float largest = AngleLength(previous);
    float step = 0.0f;
    for (int i = 0; i < 90; ++i)
    {
        trauma.Tick();
        const NS::Core::Vector3 now = trauma.Angles();
        largest = std::max(largest, AngleLength(now));
        step = std::max(step, AngleLength(now - previous));
        previous = now;
    }
    ASSERT_GT(largest, 0.5f);
    EXPECT_LT(step / largest, 0.6f);
}

// 同じ種と同じ足し方は同じ揺れ
TEST(CameraTrauma, SameSeedGivesTheSameShake)
{
    NS::Obj::CameraTraumaModifier first;
    NS::Obj::CameraTraumaModifier second;
    first.AddTrauma(TraumaOf(0.8f));
    second.AddTrauma(TraumaOf(0.8f));
    for (int i = 0; i < 20; ++i)
    {
        first.Tick();
        second.Tick();
        EXPECT_EQ(first.Angles(), second.Angles());
    }
}

// 一撃は足したフレームに山を置けば、そのフレームが一番大きく、向きは渡した側
TEST(CameraTrauma, KickPeaksOnItsFrameTowardItsDirection)
{
    NS::Obj::CameraTraumaModifier trauma;
    NS::Obj::CameraTraumaDesc desc;
    desc.kick.degrees = 3.0f;
    desc.kick.peakFrames = 1;
    desc.kick.direction = NS::Core::Vector2{-1.0f, 0.0f};
    trauma.AddTrauma(desc);
    const NS::Core::Vector3 first = trauma.Angles();
    EXPECT_FLOAT_EQ(first.x, -3.0f);
    float previous = std::fabs(first.x);
    for (int i = 0; i < 6; ++i)
    {
        trauma.Tick();
        if (i == 0)
        {
            continue;
        }
        EXPECT_LT(std::fabs(trauma.Angles().x), previous);
        previous = std::fabs(trauma.Angles().x);
    }
    for (int i = 0; i < 30; ++i)
    {
        trauma.Tick();
    }
    EXPECT_TRUE(trauma.IsFinished());
}

// 溜めは毎フレーム保ち、保たなくなったフレームから減る
TEST(CameraTrauma, HeldTraumaStaysUntilReleased)
{
    NS::Obj::CameraTraumaModifier trauma;
    for (int i = 0; i < 10; ++i)
    {
        trauma.HoldTrauma(0.6f, ShapeOf(10.0f, 1.0f));
        trauma.Tick();
        EXPECT_FLOAT_EQ(trauma.Trauma(), 0.6f);
    }
    trauma.Tick();
    EXPECT_LT(trauma.Trauma(), 0.6f);
}

// 揺れは描く姿勢にだけ掛かり、遊びが読む水平の前は変わらない。倍率 0 で揺れが消える
TEST(CameraTrauma, ShakesTheDrawnPoseOnlyAndScalesBySetting)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Core::Vector3{0.0f, 0.0f, -5.0f}, NS::Core::Vector3{}), nullptr);
    NS::Obj::CameraManager* cameras = scene.GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Camera);
    const NS::Core::Vector3 forwardBefore = cameras->ForwardHorizontal();
    const std::optional<NS::Obj::CameraPose> calm = cameras->ComposePose(1.0f);
    ASSERT_TRUE(calm.has_value());

    NS::Obj::CameraTraumaDesc desc = TraumaOf(1.0f);
    desc.kick.degrees = 3.0f;
    desc.kick.direction = NS::Core::Vector2{1.0f, 0.0f};
    ASSERT_TRUE(cameras->AddTrauma(desc));
    EXPECT_FLOAT_EQ(cameras->Trauma(), 1.0f);
    const std::optional<NS::Obj::CameraPose> shaken = cameras->ComposePose(1.0f);
    ASSERT_TRUE(shaken.has_value());
    // 位置は動かさず、視線を回す
    EXPECT_EQ(shaken->position, calm->position);
    EXPECT_GT((shaken->target - calm->target).Length(), 0.01f);
    EXPECT_EQ(cameras->ForwardHorizontal(), forwardBefore);

    cameras->SetShakeScale(0.0f);
    const std::optional<NS::Obj::CameraPose> muted = cameras->ComposePose(1.0f);
    ASSERT_TRUE(muted.has_value());
    EXPECT_LT((muted->target - calm->target).Length(), 1.0e-5f);
    cameras->SetShakeScale(0.5f);
    const std::optional<NS::Obj::CameraPose> half = cameras->ComposePose(1.0f);
    ASSERT_TRUE(half.has_value());
    EXPECT_NEAR((half->target - calm->target).Length(), (shaken->target - calm->target).Length() * 0.5f, 1.0e-3f);
    EXPECT_FALSE(cameras->AddTrauma(TraumaOf(-1.0f)));
}

// 次の当たりで揺れと寄りを止めても、トラウマは残って足される
TEST(CameraTrauma, HitEffectsStopKeepsTheTrauma)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Core::Vector3{0.0f, 0.0f, -5.0f}, NS::Core::Vector3{}), nullptr);
    NS::Obj::CameraManager* cameras = scene.GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    ASSERT_TRUE(cameras->AddTrauma(TraumaOf(0.4f)));
    ASSERT_TRUE(cameras->StartZoomRoll(NS::Obj::CameraZoomRollDesc{.zoom = 1.2f, .holdFrames = 5}));
    NS::Obj::StopCameraHitEffects(scene);
    EXPECT_FLOAT_EQ(cameras->ZoomRoll().zoom, 1.0f);
    EXPECT_FLOAT_EQ(cameras->Trauma(), 0.4f);
    NS::Obj::StopCameraEffects(scene);
    EXPECT_FLOAT_EQ(cameras->Trauma(), 0.0f);
}

// 弱い保ちは強い揺れの形を奪わない。外れの揺れの途中に溜め始めても、外れの揺れのまま減る
TEST(CameraTrauma, WeakerHoldKeepsTheStrongerShape)
{
    NS::Obj::CameraTraumaModifier trauma;
    trauma.AddTrauma(TraumaOf(0.9f));
    NS::Obj::CameraTraumaShape charge = ShapeOf(20.0f, 4.0f);
    charge.exponent = 3.0f;
    trauma.HoldTrauma(0.2f, charge);
    EXPECT_FLOAT_EQ(trauma.ShakeAmount(), 0.81f);
    trauma.HoldTrauma(0.95f, charge);
    EXPECT_NEAR(trauma.ShakeAmount(), 0.95f * 0.95f * 0.95f, 1.0e-5f);
}
