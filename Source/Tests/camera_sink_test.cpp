#include "Runtime/Core/CameraData.h"
#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <optional>

// 真ん中のカメラの揺れ: 画面ごと下へ沈み、底で震え、こらえ、反動の頭で跳ね返る 4 拍
// 揺れは射影の画面のずれで掛け、カメラの位置と向きは変えない

namespace
{
    constexpr float k_PixelToScreen = 2.0f / 1080.0f; // 高さ 1080 の画面の 1 画素の、画面の座標 (縦 -1〜1) での幅

    NS::Obj::CameraSinkDesc SinkOf()
    {
        NS::Obj::CameraSinkDesc desc;
        desc.bottomPixels = 8.0f;
        desc.sinkFrames = 2;
        desc.tremblePixels = 5.0f;
        desc.trembleFrames = 6;
        desc.tremblePeriodFrames = 2;
        desc.bounceStartFrame = 12;
        desc.overshootRatio = 0.5f;
        desc.bouncePeriodFrames = 8;
        desc.frames = 32;
        return desc;
    }

    // 世界の点を射影して、画面の座標 (縦横 -1〜1) を返す
    NS::Core::Vector2 Project(const NS::Core::CameraData& camera, const NS::Core::Vector3& point)
    {
        const NS::Core::Vector4 clip =
            NS::Core::Vector4::Transform(NS::Core::Vector4{point.x, point.y, point.z, 1.0f}, camera.ViewProjection());
        return NS::Core::Vector2{clip.x / clip.w, clip.y / clip.w};
    }
} // namespace

// 画面のずれは、どの奥行きの点も同じだけずらす。カメラとの距離で揺れの画素数が変わらない
TEST(CameraSink, ScreenOffsetShiftsEveryDepthByTheSameAmount)
{
    NS::Core::CameraData camera;
    camera.SetPosition(NS::Core::Vector3{0.0f, 2.0f, -6.0f});
    camera.SetTarget(NS::Core::Vector3{0.0f, 1.0f, 0.0f});
    const NS::Core::Matrix viewBefore = camera.View();
    const NS::Core::Vector3 nearPoint{0.5f, 1.0f, 0.0f};
    const NS::Core::Vector3 farPoint{-3.0f, 0.0f, 40.0f};
    const NS::Core::Vector2 nearBefore = Project(camera, nearPoint);
    const NS::Core::Vector2 farBefore = Project(camera, farPoint);
    camera.SetScreenOffset(NS::Core::Vector2{0.01f, -0.02f});
    EXPECT_EQ(camera.View(), viewBefore);
    const NS::Core::Vector2 nearAfter = Project(camera, nearPoint);
    const NS::Core::Vector2 farAfter = Project(camera, farPoint);
    EXPECT_NEAR(nearAfter.x - nearBefore.x, 0.01f, 1.0e-5f);
    EXPECT_NEAR(nearAfter.y - nearBefore.y, -0.02f, 1.0e-5f);
    EXPECT_NEAR(farAfter.x - farBefore.x, 0.01f, 1.0e-5f);
    EXPECT_NEAR(farAfter.y - farBefore.y, -0.02f, 1.0e-5f);
}

// 沈む: 2 フレーム目で底に届く。震える: 底を中心に揺れて弱まる。こらえる: 跳ね返りの頭まで底のまま
TEST(CameraSink, SinksTremblesAndHoldsAtTheBottom)
{
    const NS::Obj::CameraSinkDesc desc = SinkOf();
    const float bottom = -desc.bottomPixels;
    EXPECT_NEAR(NS::Obj::CameraSinkPixelsAt(desc, 0), bottom * std::sin(std::numbers::pi_v<float> * 0.25f), 1.0e-4f);
    EXPECT_FLOAT_EQ(NS::Obj::CameraSinkPixelsAt(desc, 1), bottom);
    EXPECT_FLOAT_EQ(NS::Obj::CameraSinkPixelsAt(desc, 2), bottom + desc.tremblePixels);
    EXPECT_NEAR(NS::Obj::CameraSinkPixelsAt(desc, 3), bottom - desc.tremblePixels * 5.0f / 6.0f, 1.0e-4f);
    for (int frame = desc.sinkFrames + desc.trembleFrames; frame <= desc.bounceStartFrame; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_FLOAT_EQ(NS::Obj::CameraSinkPixelsAt(desc, frame), bottom);
    }
}

// 跳ね返る: 跳ね返りの頭から上へ戻り、半周期で行き過ぎの割合だけ上へ出て、収まる。描き終えたら 0
TEST(CameraSink, BouncesPastZeroByTheOvershootRatio)
{
    const NS::Obj::CameraSinkDesc desc = SinkOf();
    const int peak = desc.bounceStartFrame + desc.bouncePeriodFrames / 2;
    EXPECT_GT(NS::Obj::CameraSinkPixelsAt(desc, desc.bounceStartFrame + 1), -desc.bottomPixels);
    EXPECT_NEAR(NS::Obj::CameraSinkPixelsAt(desc, peak), desc.bottomPixels * desc.overshootRatio, 1.0e-3f);
    EXPECT_LT(std::abs(NS::Obj::CameraSinkPixelsAt(desc, desc.frames - 1)), desc.bottomPixels * 0.1f);
    EXPECT_FLOAT_EQ(NS::Obj::CameraSinkPixelsAt(desc, desc.frames), 0.0f);
}

// 揺れは描く姿の画面のずれだけを動かし、位置・注視点・遊びが読む前の向きは変えない。設定の倍率も掛かる
TEST(CameraSink, ShiftsTheDrawnScreenOnlyAndScalesBySetting)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Core::Vector3{0.0f, 0.0f, -5.0f}, NS::Core::Vector3{}), nullptr);
    NS::Obj::CameraManager* cameras = scene.GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Camera);
    const NS::Core::Vector3 forwardBefore = cameras->ForwardHorizontal();
    const std::optional<NS::Obj::CameraPose> calm = cameras->ComposePose(1.0f);
    ASSERT_TRUE(calm.has_value());

    NS::Obj::CameraSinkDesc desc = SinkOf();
    desc.sinkFrames = 1;
    ASSERT_TRUE(cameras->AddModifier(NS::Obj::CameraSinkModifier::Create(desc)));
    const std::optional<NS::Obj::CameraPose> shaken = cameras->ComposePose(1.0f);
    ASSERT_TRUE(shaken.has_value());
    EXPECT_EQ(shaken->position, calm->position);
    EXPECT_EQ(shaken->target, calm->target);
    EXPECT_NEAR(shaken->screenOffset.y, -desc.bottomPixels * k_PixelToScreen, 1.0e-6f);
    EXPECT_FLOAT_EQ(shaken->screenOffset.x, 0.0f);
    EXPECT_EQ(cameras->ForwardHorizontal(), forwardBefore);

    cameras->SetShakeScale(0.5f);
    const std::optional<NS::Obj::CameraPose> half = cameras->ComposePose(1.0f);
    ASSERT_TRUE(half.has_value());
    EXPECT_NEAR(half->screenOffset.y, -desc.bottomPixels * k_PixelToScreen * 0.5f, 1.0e-6f);

    // 当たりの演出を止めると外れる
    NS::Obj::StopCameraHitEffects(scene);
    EXPECT_EQ(cameras->FindModifier<NS::Obj::CameraSinkModifier>(), nullptr);
}

// 壊れた設定は積まない
TEST(CameraSink, BrokenSettingsAreRefused)
{
    NS::Obj::CameraSinkDesc desc = SinkOf();
    desc.overshootRatio = 1.0f;
    EXPECT_EQ(NS::Obj::CameraSinkModifier::Create(desc), nullptr);
    desc = SinkOf();
    desc.frames = 0;
    EXPECT_EQ(NS::Obj::CameraSinkModifier::Create(desc), nullptr);
    desc = SinkOf();
    desc.bottomPixels = std::nanf("");
    EXPECT_EQ(NS::Obj::CameraSinkModifier::Create(desc), nullptr);
}
