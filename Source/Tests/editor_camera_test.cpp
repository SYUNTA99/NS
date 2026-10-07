#include "Editor/EditorCamera.h"

#include <gtest/gtest.h>

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
