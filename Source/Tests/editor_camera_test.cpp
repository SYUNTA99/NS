#include "Framework/Scene/EditorCameraComponent.h"
#include "Framework/Scene/GameObject.h"

#include <gtest/gtest.h>

#include <cmath>

namespace SceneNs = NS::Scene;

TEST(EditorCameraTest, InitialStateMatchesDefaults)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    EXPECT_FLOAT_EQ(cam.Yaw(), 0.0f);
    EXPECT_NEAR(cam.Pitch(), -0.5236f, 1e-4f);
    EXPECT_FLOAT_EQ(cam.Distance(), 15.0f);
}

TEST(EditorCameraTest, OrbitDeltaApplied)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    cam.ApplyOrbit(0.5f, 0.3f);
    EXPECT_FLOAT_EQ(cam.Yaw(), 0.5f);
    // -0.5236 + 0.3 = -0.2236、 clamp 範囲内
    EXPECT_NEAR(cam.Pitch(), -0.2236f, 1e-4f);
}

TEST(EditorCameraTest, PitchClampedToLimits)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    cam.ApplyOrbit(0.0f, +10.0f);
    EXPECT_NEAR(cam.Pitch(), SceneNs::EditorCameraComponent::kPitchMax, 1e-4f);
    cam.ApplyOrbit(0.0f, -100.0f);
    EXPECT_NEAR(cam.Pitch(), SceneNs::EditorCameraComponent::kPitchMin, 1e-4f);
}

TEST(EditorCameraTest, DistanceClampedViaSetDistance)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    cam.SetDistance(1000.0f);
    EXPECT_NEAR(cam.Distance(), SceneNs::EditorCameraComponent::kMaxDistance, 1e-4f);
    cam.SetDistance(-50.0f);
    EXPECT_NEAR(cam.Distance(), SceneNs::EditorCameraComponent::kMinDistance, 1e-4f);
}

TEST(EditorCameraTest, ComputeCameraPositionForYawZeroPlacesCameraOnZAxis)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    cam.SetCenter({0.0f, 0.0f, 0.0f});
    cam.SetYawPitch(0.0f, -0.0873f); // pitch ≒ 0 (clamp 上端)
    cam.SetDistance(10.0f);

    auto pos = cam.ComputeCameraPosition();
    // pitch ≒ 0 で yaw 0 → cos*sin = 0, sin = -0.0873 ≒ -0.87 で y は少し下、
    // z = cos(-0.0873) * 10 ≒ 9.96
    EXPECT_NEAR(pos.x, 0.0f, 1e-3f);
    EXPECT_NEAR(pos.z, 10.0f, 0.2f);
}

TEST(EditorCameraTest, ComputeCameraPositionForYaw90PlacesCameraOnXAxis)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    cam.SetCenter({0.0f, 0.0f, 0.0f});
    cam.SetYawPitch(1.5707963f, -0.0873f);
    cam.SetDistance(10.0f);

    auto pos = cam.ComputeCameraPosition();
    EXPECT_NEAR(pos.x, 10.0f, 0.2f);
    EXPECT_NEAR(pos.z, 0.0f, 1e-3f);
}

TEST(EditorCameraTest, PanShiftsCenter)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);

    const auto before = cam.Center();
    cam.ApplyPan(1.0f, 0.5f);
    const auto after = cam.Center();
    // yaw=0 で right ≒ (1,0,0)、 up = (0,1,0)、 scale = distance*0.1 = 1.5
    EXPECT_NEAR(after.x - before.x, 1.5f, 1e-3f);
    EXPECT_NEAR(after.y - before.y, 0.75f, 1e-3f);
    EXPECT_NEAR(after.z - before.z, 0.0f, 1e-3f);
}

TEST(EditorCameraTest, OnUpdateNoOpWhenInputNullAndCameraNull)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);
    // crash しないことを確認
    cam.OnUpdate();
    SUCCEED();
}

TEST(EditorCameraTest, OnUpdateSkippedWhenInactive)
{
    SceneNs::GameObject obj;
    SceneNs::EditorCameraComponent cam(&obj);
    cam.SetActive(false);
    cam.SetYawPitch(1.0f, -0.5f);
    cam.OnUpdate();
    EXPECT_FLOAT_EQ(cam.Yaw(), 1.0f); // 変化なし (Apply* 呼ばれず)
}
