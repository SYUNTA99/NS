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
    EXPECT_FALSE(
        GL::Player::PlayerJudgeBrake::Judge(false, desired, NS::Vector3{-1.0f, 0.0f, 0.0f}, -0.8f));
    EXPECT_FALSE(GL::Player::PlayerJudgeBrake::Judge(
        true, NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector3{-1.0f, 0.0f, 0.0f}, -0.8f));
    EXPECT_FALSE(GL::Player::PlayerJudgeBrake::Judge(true, desired, NS::Vector3{-0.8f, 0.0f, 0.0f}, -0.8f));
    EXPECT_TRUE(
        GL::Player::PlayerJudgeBrake::Judge(true, desired, NS::Vector3{-0.81f, 10.0f, 0.0f}, -0.8f));
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
    EXPECT_TRUE(GL::Player::PlayerJudgeJump::Judge(true, 0.0f, 1, true, 0.0f));
    EXPECT_TRUE(GL::Player::PlayerJudgeJump::Judge(false, 0.001f, 1, false, 0.001f));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(false, 0.0f, 1, true, 1.0f));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(true, 1.0f, 0, true, 1.0f));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(true, 1.0f, -1, true, 1.0f));
    EXPECT_FALSE(GL::Player::PlayerJudgeJump::Judge(true, 0.0f, 1, false, 0.0f));
    EXPECT_FALSE(
        GL::Player::PlayerJudgeJump::Judge(false, std::numeric_limits<float>::quiet_NaN(), 1, true, 0.0f));
}

TEST(PlayerJudges, BodySlamRequiresFreshReservationAndLocomotion)
{
    for (const bool spent : {false, true})
    {
        for (const bool wasSlamming : {false, true})
        {
            for (const bool locomotion : {false, true})
            {
                EXPECT_EQ(GL::Player::PlayerJudgeBodySlam::Judge(0.001f, spent, wasSlamming, locomotion),
                          !spent && !wasSlamming && locomotion);
            }
        }
    }
    EXPECT_FALSE(GL::Player::PlayerJudgeBodySlam::Judge(0.0f, false, false, true));
    EXPECT_FALSE(GL::Player::PlayerJudgeBodySlam::Judge(-0.001f, false, false, true));
    EXPECT_FALSE(
        GL::Player::PlayerJudgeBodySlam::Judge(std::numeric_limits<float>::quiet_NaN(), false, false, true));
}

TEST(PlayerJudges, LedgeEligibilityPreservesGroundUpwardAndRecentImpactExclusions)
{
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, 0.0f, false));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, -1.0f, false));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::Judge(true, -1.0f, false));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, 0.001f, false));
    EXPECT_FALSE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, -1.0f, true));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::Judge(false, std::numeric_limits<float>::quiet_NaN(), false));
}

TEST(PlayerJudges, LedgeBandIncludesItsEdgesWithoutPhysicsOrState)
{
    const NS::AABB box{NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector3{1.0f, 1.0f, 1.0f}};
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::InBand(NS::Vector3{-1.0f, 2.0f, 1.0f}, box, 0.5f, 0.25f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::InBand(NS::Vector3{1.0f, 2.5f, -1.0f}, box, 0.5f, 0.25f));
    EXPECT_TRUE(GL::Player::PlayerJudgeLedgeGrab::InBand(NS::Vector3{0.0f, 1.75f, 0.0f}, box, 0.5f, 0.25f));
    EXPECT_FALSE(
        GL::Player::PlayerJudgeLedgeGrab::InBand(NS::Vector3{1.001f, 2.0f, 0.0f}, box, 0.5f, 0.25f));
    EXPECT_FALSE(
        GL::Player::PlayerJudgeLedgeGrab::InBand(NS::Vector3{0.0f, 2.501f, 0.0f}, box, 0.5f, 0.25f));
    EXPECT_FALSE(
        GL::Player::PlayerJudgeLedgeGrab::InBand(NS::Vector3{0.0f, 1.749f, 0.0f}, box, 0.5f, 0.25f));
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

TEST(PlayerJudges, LocomotionIsAnyOfIdleWalkFallRebound)
{
    EXPECT_FALSE(GL::Player::PlayerJudgeLocomotion::Judge(false, false, false, false));
    EXPECT_TRUE(GL::Player::PlayerJudgeLocomotion::Judge(true, false, false, false));
    EXPECT_TRUE(GL::Player::PlayerJudgeLocomotion::Judge(false, true, false, false));
    EXPECT_TRUE(GL::Player::PlayerJudgeLocomotion::Judge(false, false, true, false));
    EXPECT_TRUE(GL::Player::PlayerJudgeLocomotion::Judge(false, false, false, true));
}
