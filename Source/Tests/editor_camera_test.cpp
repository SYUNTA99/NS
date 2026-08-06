#include "Editor/EditorCamera.h"

#include <gtest/gtest.h>

using NS::Editor::EditorCamera;
using NS::Editor::EditorCameraInput;

TEST(EditorCameraTest, InitialStateMatchesDefaults)
{
    EditorCamera cam;

    EXPECT_FLOAT_EQ(cam.Yaw(), 0.0f);
    EXPECT_NEAR(cam.Pitch(), -0.5236f, 1e-4f);
    EXPECT_FLOAT_EQ(cam.Distance(), 15.0f);
}

TEST(EditorCameraTest, OrbitDeltaApplied)
{
    EditorCamera cam;

    cam.ApplyOrbit(0.5f, 0.3f);
    EXPECT_FLOAT_EQ(cam.Yaw(), 0.5f);
    // -0.5236 + 0.3 = -0.2236、 clamp 範囲内
    EXPECT_NEAR(cam.Pitch(), -0.2236f, 1e-4f);
}

TEST(EditorCameraTest, PitchClampedToLimits)
{
    EditorCamera cam;

    cam.ApplyOrbit(0.0f, +10.0f);
    EXPECT_NEAR(cam.Pitch(), EditorCamera::k_PitchMax, 1e-4f);
    cam.ApplyOrbit(0.0f, -100.0f);
    EXPECT_NEAR(cam.Pitch(), EditorCamera::k_PitchMin, 1e-4f);
}

TEST(EditorCameraTest, DistanceClampedViaSetDistance)
{
    EditorCamera cam;

    cam.SetDistance(EditorCamera::k_MaxDistance + 1000.0f);
    EXPECT_NEAR(cam.Distance(), EditorCamera::k_MaxDistance, 1e-4f);
    cam.SetDistance(-50.0f);
    EXPECT_NEAR(cam.Distance(), EditorCamera::k_MinDistance, 1e-4f);
}

TEST(EditorCameraTest, FlyMoveForwardFollowsLookDirection)
{
    EditorCamera cam;
    cam.SetYawPitch(0.0f, 0.0f); // 水平で -Z を向く
    cam.SetDistance(10.0f);
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});

    // yaw=0/pitch=0 で前進(W)は視線方向 -Z。 移動量は KeyMoveSpeed 0.6 × distance 10 × dt 1 = 6
    cam.ApplyFlyMove(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(cam.Center().z, -6.0f, 1e-4f);
    EXPECT_NEAR(cam.Center().x, 0.0f, 1e-4f);
    EXPECT_NEAR(cam.Center().y, 0.0f, 1e-4f);
}

// 立方体 1 個へ寄せた距離でも WASD の速さが残る
// 距離比例のままだと注視した途端に歩くより遅くなり、 別の場所へ移るのに時間がかかる
TEST(EditorCameraTest, FlyMoveKeepsSpeedWhenZoomedIn)
{
    EditorCamera cam;
    cam.SetYawPitch(0.0f, 0.0f);
    cam.SetDistance(EditorCamera::k_MinDistance);
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});

    // 下限 6 × KeyMoveSpeed 0.6 × dt 1 = 3.6。 素の距離 2 だと 1.2 しか進まない
    cam.ApplyFlyMove(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(cam.Center().z, -3.6f, 1e-4f);
}

TEST(EditorCameraTest, FlyMoveForwardIncludesPitch)
{
    EditorCamera cam;
    cam.SetYawPitch(0.0f, -0.5f); // 上を向く
    cam.SetDistance(10.0f);
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});

    // pitch を含むので W は見上げた方向、 すなわち高さも上がる (旧 ApplyKeyMove は水平のみだった)
    cam.ApplyFlyMove(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_GT(cam.Center().y, 0.0f);
    EXPECT_LT(cam.Center().z, 0.0f);
}

TEST(EditorCameraTest, FlyMoveStrafeGoesScreenRight)
{
    EditorCamera cam;
    cam.SetYawPitch(0.0f, 0.0f);
    cam.SetDistance(10.0f);
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});

    // LH view では yaw=0 の画面右は -X。 D(strafe +1) はそちらへ動く (左右反転バグの回帰防止)
    cam.ApplyFlyMove(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_NEAR(cam.Center().x, -6.0f, 1e-4f);
    EXPECT_NEAR(cam.Center().z, 0.0f, 1e-4f);
    EXPECT_NEAR(cam.Center().y, 0.0f, 1e-4f);
}

TEST(EditorCameraTest, FlyMoveVerticalUsesWorldUp)
{
    EditorCamera cam;
    cam.SetYawPitch(0.7f, -0.3f); // 向きに依らず上下は world 軸
    cam.SetDistance(10.0f);
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});

    cam.ApplyFlyMove(0.0f, 0.0f, 1.0f, 1.0f); // E 相当
    EXPECT_NEAR(cam.Center().y, 6.0f, 1e-4f);
    EXPECT_NEAR(cam.Center().x, 0.0f, 1e-4f);
    EXPECT_NEAR(cam.Center().z, 0.0f, 1e-4f);
}

TEST(EditorCameraTest, LookKeepsEyeFixed)
{
    EditorCamera cam;
    cam.SetYawPitch(0.0f, -0.2f);
    cam.SetDistance(10.0f);
    cam.SetCenter(NS::Math::Vector3{1.0f, 2.0f, 3.0f});

    // その場の見回し: yaw/pitch は変わるが eye は不変 (orbit ではない)
    const auto eyeBefore = cam.ComputeCameraPosition();
    cam.ApplyLook(0.4f, 0.1f);
    const auto eyeAfter = cam.ComputeCameraPosition();
    EXPECT_NEAR(eyeAfter.x, eyeBefore.x, 1e-3f);
    EXPECT_NEAR(eyeAfter.y, eyeBefore.y, 1e-3f);
    EXPECT_NEAR(eyeAfter.z, eyeBefore.z, 1e-3f);
    EXPECT_NEAR(cam.Yaw(), 0.4f, 1e-4f);
    EXPECT_NEAR(cam.Pitch(), -0.1f, 1e-4f);
}

TEST(EditorCameraTest, ComputeCameraPositionForYawZeroPlacesCameraOnZAxis)
{
    EditorCamera cam;

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
    EditorCamera cam;

    cam.SetCenter({0.0f, 0.0f, 0.0f});
    cam.SetYawPitch(1.5707963f, -0.0873f);
    cam.SetDistance(10.0f);

    auto pos = cam.ComputeCameraPosition();
    EXPECT_NEAR(pos.x, 10.0f, 0.2f);
    EXPECT_NEAR(pos.z, 0.0f, 1e-3f);
}

TEST(EditorCameraTest, PanShiftsCenter)
{
    EditorCamera cam;

    const auto before = cam.Center();
    cam.ApplyPan(1.0f, 0.5f);
    const auto after = cam.Center();
    // yaw=0 で right ≒ (1,0,0)、 up = (0,1,0)、 scale = distance*0.1 = 1.5
    EXPECT_NEAR(after.x - before.x, 1.5f, 1e-3f);
    EXPECT_NEAR(after.y - before.y, 0.75f, 1e-3f);
    EXPECT_NEAR(after.z - before.z, 0.0f, 1e-3f);
}

TEST(EditorCameraTest, PoseCarriesProjectionSettings)
{
    EditorCamera cam;
    cam.SetNearPlane(0.5f);
    cam.SetFarPlane(3000.0f);
    cam.SetCenter(NS::Math::Vector3{1.0f, 2.0f, 3.0f});

    const NS::Object::CameraPose pose = cam.Pose();
    EXPECT_FLOAT_EQ(pose.nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(pose.farPlane, 3000.0f);
    EXPECT_FLOAT_EQ(pose.target.x, 1.0f);
    EXPECT_FLOAT_EQ(pose.target.y, 2.0f);
    EXPECT_FLOAT_EQ(pose.target.z, 3.0f);
}

TEST(EditorCameraTest, TickDoesNotCrashHeadless)
{
    EditorCamera cam;
    // 窓もデバイスも無い環境で Platform Input を読んでも落ちないことを確認
    cam.Tick();
    SUCCEED();
}

TEST(EditorCameraTest, ApplyInputFlyingLooksAndKeepsEyeFixed)
{
    EditorCamera cam;
    cam.SetCenter(NS::Math::Vector3{1.0f, 2.0f, 3.0f});
    cam.SetDistance(10.0f);
    const auto eyeBefore = cam.ComputeCameraPosition();

    EditorCameraInput input{};
    input.flying = true;
    input.lookYawPixels = 100.0f;
    cam.ApplyInput(input);

    // 既定感度 m_mouseSensOrbit = 0.003
    EXPECT_NEAR(cam.Yaw(), 100.0f * 0.003f, 1e-4f);
    const auto eyeAfter = cam.ComputeCameraPosition();
    EXPECT_NEAR(eyeAfter.x, eyeBefore.x, 1e-3f);
    EXPECT_NEAR(eyeAfter.y, eyeBefore.y, 1e-3f);
    EXPECT_NEAR(eyeAfter.z, eyeBefore.z, 1e-3f);
}

TEST(EditorCameraTest, ApplyInputIgnoresLookAndMoveWhenNotFlying)
{
    EditorCamera cam;
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});
    const auto centerBefore = cam.Center();

    EditorCameraInput input{};
    input.flying = false;
    input.lookYawPixels = 100.0f;
    input.forwardAxis = 1.0f;
    input.deltaSeconds = 1.0f;
    cam.ApplyInput(input);

    EXPECT_FLOAT_EQ(cam.Yaw(), 0.0f);
    EXPECT_NEAR(cam.Center().x, centerBefore.x, 1e-4f);
    EXPECT_NEAR(cam.Center().y, centerBefore.y, 1e-4f);
    EXPECT_NEAR(cam.Center().z, centerBefore.z, 1e-4f);
}

TEST(EditorCameraTest, ApplyInputFlyingMovesForward)
{
    EditorCamera cam;
    cam.SetYawPitch(0.0f, 0.0f);
    cam.SetDistance(10.0f);
    cam.SetCenter(NS::Math::Vector3{0.0f, 0.0f, 0.0f});

    EditorCameraInput input{};
    input.flying = true;
    input.forwardAxis = 1.0f;
    input.deltaSeconds = 1.0f;
    cam.ApplyInput(input);

    // yaw=0/pitch=0 の前進は -Z 方向 (FlyMoveForwardFollowsLookDirection と同じ期待)
    EXPECT_LT(cam.Center().z, 0.0f);
}

TEST(EditorCameraTest, ApplyInputWheelZoomsInOverSpringSteps)
{
    EditorCamera cam;
    cam.SetDistance(10.0f);
    const float before = cam.Distance();

    EditorCameraInput input{};
    input.flying = false;
    input.wheelNotches = 1.0f;
    input.deltaSeconds = 0.1f;
    for (int i = 0; i < 10; ++i)
    {
        cam.ApplyInput(input);
    }

    EXPECT_LT(cam.Distance(), before);
}

TEST(EditorCameraTest, ApplyInputPanShiftsCenterWhenNotFlying)
{
    EditorCamera cam;
    const auto before = cam.Center();

    EditorCameraInput input{};
    input.flying = false;
    input.panXPixels = 1.0f;
    cam.ApplyInput(input);

    const auto after = cam.Center();
    EXPECT_NEAR(after.x - before.x, 1.0f * cam.Distance() * 0.1f * 0.02f, 1e-3f);
}
