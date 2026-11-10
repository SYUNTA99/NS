#include "Game/Player/PlayerJudges.h"

#include <gtest/gtest.h>

#include <limits>

TEST(PlayerJudges, GroundedWalkAndIdleAreExclusive)
{
    for (const bool grounded : {false, true})
    {
        for (const bool input : {false, true})
        {
            for (const bool stopped : {false, true})
            {
                const bool walk = GL::Player::PlayerJudgeWalk::Judge(grounded, input, stopped);
                const bool idle = GL::Player::PlayerJudgeIdle::Judge(grounded, walk);
                EXPECT_EQ(walk, grounded && (input || !stopped));
                EXPECT_EQ(idle, grounded && !walk);
            }
        }
    }
}

TEST(PlayerJudges, BrakeUsesHorizontalInputAndStrictThreshold)
{
    const NS::Vector3 desired{2.0f, 8.0f, 0.0f};
    EXPECT_FALSE(GL::Player::PlayerJudgeBrake::Judge(false, desired, NS::Vector3{-1.0f, 0.0f, 0.0f}, -0.8f));
    EXPECT_FALSE(GL::Player::PlayerJudgeBrake::Judge(
        true, NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector3{-1.0f, 0.0f, 0.0f}, -0.8f));
    EXPECT_FALSE(GL::Player::PlayerJudgeBrake::Judge(true, desired, NS::Vector3{-0.8f, 0.0f, 0.0f}, -0.8f));
    EXPECT_TRUE(GL::Player::PlayerJudgeBrake::Judge(true, desired, NS::Vector3{-0.81f, 10.0f, 0.0f}, -0.8f));
}

TEST(PlayerJudges, UpwardReboundCannotLandOnStaleGroundFlag)
{
    EXPECT_FALSE(GL::Player::PlayerJudgeLand::Judge(true, 0.01f));
    EXPECT_FALSE(GL::Player::PlayerJudgeLand::Judge(false, -1.0f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLand::Judge(true, 0.0f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLand::Judge(true, -1.0f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLand::Judge(true, std::numeric_limits<float>::quiet_NaN()));
}

TEST(PlayerJudges, JumpKeepsCoyoteBufferAndRemainingCountBoundaries)
{
    EXPECT_TRUE(GL::Player::PlayerJudgeJump::Judge(true, 0, 1, true, 0));
    EXPECT_TRUE(GL::Player::PlayerJudgeJump::Judge(false, 1, 1, false, 1));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(false, 0, 1, true, 1));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(true, 1, 0, true, 1));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(true, 1, -1, true, 1));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(true, 0, 1, false, 0));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(false, -1, 1, true, 0));
}

TEST(PlayerJudges, LedgeEligibilityPreservesGroundAndUpwardExclusions)
{
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, 0.0f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, -1.0f));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::Judge(true, -1.0f));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, 0.001f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, std::numeric_limits<float>::quiet_NaN()));
}

TEST(PlayerJudges, LedgeBandIncludesItsEdgesWithoutPhysicsOrState)
{
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::InBand(2.0f, 1.5f, 0.5f, 0.25f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::InBand(2.0f, 2.25f, 0.5f, 0.25f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::InBand(2.0f, 2.0f, 0.5f, 0.25f));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::InBand(2.0f, 1.499f, 0.5f, 0.25f));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::InBand(2.0f, 2.251f, 0.5f, 0.25f));
    // 上面か手の高さが非数なら掴まない
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::InBand(2.0f, std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.25f));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::InBand(std::numeric_limits<float>::quiet_NaN(), 2.0f, 0.5f, 0.25f));
}

TEST(PlayerJudges, MoveInputIncludesTheDeadzoneEdgeAndStoppedNeedsExactZero)
{
    EXPECT_TRUE(GL::Player::PlayerJudgeMoveInput::Judge(0.2f, 0.2f));
    EXPECT_FALSE(GL::Player::PlayerJudgeMoveInput::Judge(0.199f, 0.2f));
    EXPECT_FALSE(GL::Player::PlayerJudgeMoveInput::Judge(std::numeric_limits<float>::quiet_NaN(), 0.2f));
    EXPECT_TRUE(GL::Player::PlayerJudgeStopped::Judge(NS::Vector3{0.0f, 5.0f, -0.0f}));
    EXPECT_FALSE(GL::Player::PlayerJudgeStopped::Judge(NS::Vector3{0.0001f, 0.0f, 0.0f}));
}

TEST(PlayerJudges, FallAndLedgeChoicesFollowTheirOneSignal)
{
    EXPECT_TRUE(GL::Player::PlayerJudgeFall::Judge(false));
    EXPECT_FALSE(GL::Player::PlayerJudgeFall::Judge(true));
    EXPECT_TRUE(GL::Player::PlayerJudgeClimbLedge::Judge(0.001f));
    EXPECT_FALSE(GL::Player::PlayerJudgeClimbLedge::Judge(0.0f));
    EXPECT_FALSE(GL::Player::PlayerJudgeClimbLedge::Judge(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_TRUE(GL::Player::PlayerJudgeDropLedge::Judge(true));
    EXPECT_FALSE(GL::Player::PlayerJudgeDropLedge::Judge(false));
}
