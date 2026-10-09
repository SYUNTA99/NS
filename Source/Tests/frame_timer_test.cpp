#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

// 固定更新の回数の決まりを縛る。詰まりの後に世界を早送りしない

// 1 秒の詰まりでも上限の回数だけ回し、超えた分の貯めは捨てる
TEST(FrameTimer, LongStallRunsAtMostTheCapAndDropsTheRest)
{
    const float fixedDelta = 1.0f / 60.0f;
    const NS::OS::FixedStepAdvance advance = NS::OS::AdvanceFixedSteps(0.0f, 1.0f, fixedDelta);
    EXPECT_EQ(advance.steps, NS::OS::k_MaxFixedStepsPerFrame);
    EXPECT_FLOAT_EQ(advance.accumulator, 0.0f);
}

TEST(FrameTimer, ShortFrameKeepsTheRemainder)
{
    const float fixedDelta = 0.01f;
    const NS::OS::FixedStepAdvance advance = NS::OS::AdvanceFixedSteps(0.004f, 0.021f, fixedDelta);
    EXPECT_EQ(advance.steps, 2);
    EXPECT_NEAR(advance.accumulator, 0.005f, 1.0e-6f);
}
