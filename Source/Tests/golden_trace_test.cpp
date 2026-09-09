#include "golden_trace.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Tests::CompareTraces;
    using NS::Tests::FormatTrace;
    using NS::Tests::ParseTrace;
    using NS::Tests::StepRecord;
    using NS::Tests::TraceDiff;
    using NS::Tests::TraceTolerance;

    constexpr TraceTolerance k_Exact{};

    //! 1 歩ごとに同じ量だけ進む軌跡。全歩が接地
    std::vector<StepRecord> MakeRamp(int steps)
    {
        std::vector<StepRecord> trace;
        for (int i = 0; i < steps; ++i)
        {
            const float t = static_cast<float>(i);
            trace.push_back(
                StepRecord{Vector3{t * 0.1f, 1.0f + t * 0.01f, t * 0.2f}, Vector3{0.1f, 0.01f, 0.2f}, true});
        }
        return trace;
    }
} // namespace

TEST(GoldenTraceCompare, IdenticalTracesMatch)
{
    const std::vector<StepRecord> trace = MakeRamp(20);

    const TraceDiff diff = CompareTraces(trace, trace, k_Exact);
    EXPECT_TRUE(diff.matched);
    EXPECT_EQ(diff.maxPositionDelta, 0.0f);
    EXPECT_EQ(diff.groundedMismatches, 0u);
}

TEST(GoldenTraceCompare, ShiftedStepIsReported)
{
    const std::vector<StepRecord> baseline = MakeRamp(20);
    std::vector<StepRecord> actual = baseline;
    actual[7].position.z += 0.5f;

    const TraceDiff diff = CompareTraces(baseline, actual, k_Exact);
    EXPECT_FALSE(diff.matched);
    EXPECT_EQ(diff.firstDivergedStep, 7u);
    EXPECT_NEAR(diff.maxPositionDelta, 0.5f, 1.0e-5f);
}

TEST(GoldenTraceCompare, VelocityDivergenceIsReported)
{
    const std::vector<StepRecord> baseline = MakeRamp(20);
    std::vector<StepRecord> actual = baseline;
    actual[11].velocity.x -= 2.0f;

    const TraceDiff diff = CompareTraces(baseline, actual, k_Exact);
    EXPECT_FALSE(diff.matched);
    EXPECT_EQ(diff.firstDivergedStep, 11u);
    EXPECT_NEAR(diff.maxVelocityDelta, 2.0f, 1.0e-5f);
}

TEST(GoldenTraceCompare, DriftInsideToleranceMatches)
{
    const std::vector<StepRecord> baseline = MakeRamp(20);
    std::vector<StepRecord> actual = baseline;
    for (StepRecord& s : actual)
        s.position.y += 0.002f;

    const TraceTolerance loose{0.01f, 0.01f, 0};
    const TraceDiff diff = CompareTraces(baseline, actual, loose);
    EXPECT_TRUE(diff.matched);
    EXPECT_NEAR(diff.maxPositionDelta, 0.002f, 1.0e-5f);
}

TEST(GoldenTraceCompare, StepCountMismatchFails)
{
    const TraceDiff diff = CompareTraces(MakeRamp(20), MakeRamp(19), k_Exact);
    EXPECT_FALSE(diff.matched);
    EXPECT_EQ(diff.baselineSteps, 20u);
    EXPECT_EQ(diff.actualSteps, 19u);
}

TEST(GoldenTraceCompare, GroundedMismatchIsCountedWithPositionAllowed)
{
    const std::vector<StepRecord> baseline = MakeRamp(20);
    std::vector<StepRecord> actual = baseline;
    actual[3].grounded = false;
    actual[4].grounded = false;

    const TraceTolerance positionAllowed{1.0f, 1.0f, 0};
    const TraceDiff diff = CompareTraces(baseline, actual, positionAllowed);
    EXPECT_EQ(diff.groundedMismatches, 2u);
    EXPECT_FALSE(diff.matched);
}

TEST(GoldenTraceIo, FormatAndParseRoundTrip)
{
    const std::vector<StepRecord> trace = MakeRamp(12);

    const std::optional<std::vector<StepRecord>> parsed = ParseTrace(FormatTrace(trace));
    ASSERT_TRUE(parsed.has_value());

    const TraceDiff diff = CompareTraces(trace, *parsed, k_Exact);
    EXPECT_TRUE(diff.matched) << "書いて読み直した値が元と一致しない";
}

TEST(GoldenTraceIo, MalformedTextIsRejected)
{
    EXPECT_FALSE(ParseTrace("これは軌跡ではない").has_value());
    EXPECT_FALSE(ParseTrace("0 1.0 2.0").has_value());
}
