#include "Editor/EditorCamera.h"

#include <gtest/gtest.h>

#include <limits>

TEST(EditorCamera, BoostScalesTheFlyMove)
{
    NS::Editor::EditorCameraInput input;
    input.flying = true;
    input.forwardAxis = 1.0f;
    input.deltaSeconds = 0.1f;

    NS::Editor::EditorCamera plain;
    const NS::Vector3 plainStart = plain.Center();
    plain.ApplyInput(input);
    const float plainMove = (plain.Center() - plainStart).Length();

    NS::Editor::EditorCamera boosted;
    boosted.Tuning().boostMoveScale = 3.0f;
    const NS::Vector3 boostedStart = boosted.Center();
    input.boost = true;
    boosted.ApplyInput(input);
    const float boostedMove = (boosted.Center() - boostedStart).Length();

    ASSERT_GT(plainMove, 0.0f);
    EXPECT_FLOAT_EQ(boostedMove, plainMove * 3.0f);
}

// Inspector の手入力で 0・負・非数・無限が入っても、姿勢を壊さず動かさない
TEST(EditorCamera, InvalidTuningLeavesThePoseUnchanged)
{
    using Tuning = NS::Editor::EditorCamera::FeelTuning;
    float Tuning::* const fields[] = {
        &Tuning::springOmega,  &Tuning::mouseSensOrbit, &Tuning::mouseSensPan,
        &Tuning::mouseSensZoom, &Tuning::padSensOrbit,  &Tuning::padSensPan,
        &Tuning::padSensZoom,  &Tuning::keyMoveSpeed,   &Tuning::boostMoveScale,
    };
    const float badValues[] = {
        0.0f,
        -1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };

    NS::Editor::EditorCameraInput input;
    input.flying = true;
    input.lookYawPixels = 4.0f;
    input.lookPitchPixels = 3.0f;
    input.panXPixels = 5.0f;
    input.panYPixels = 2.0f;
    input.wheelNotches = 1.0f;
    input.forwardAxis = 1.0f;
    input.strafeAxis = 1.0f;
    input.verticalAxis = 1.0f;
    input.boost = true;
    input.deltaSeconds = 0.1f;

    for (float Tuning::* const field : fields)
    {
        for (const float bad : badValues)
        {
            NS::Editor::EditorCamera camera;
            const NS::CameraPose before = camera.Pose();
            camera.Tuning().*field = bad;
            for (int frame = 0; frame < 1000; ++frame)
            {
                camera.ApplyInput(input);
            }
            const NS::CameraPose after = camera.Pose();
            EXPECT_EQ(after.position.x, before.position.x) << "value " << bad;
            EXPECT_EQ(after.position.y, before.position.y) << "value " << bad;
            EXPECT_EQ(after.position.z, before.position.z) << "value " << bad;
            EXPECT_EQ(after.target.x, before.target.x) << "value " << bad;
            EXPECT_EQ(after.target.y, before.target.y) << "value " << bad;
            EXPECT_EQ(after.target.z, before.target.z) << "value " << bad;
        }
    }
}
