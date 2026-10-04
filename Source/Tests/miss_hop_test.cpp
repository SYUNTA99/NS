#include "Game/Level/MissHop.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using NS::Game::Level::MissHopDesc;
    using NS::Game::Level::MissHopVelocity;

    constexpr float k_Pi = 3.14159265358979f;

    MissHopDesc Desc()
    {
        return MissHopDesc{.heightRatio = 0.35f, .turnDegrees = 60.0f, .keep = 0.7f};
    }

    float HorizontalSpeed(const NS::Core::Vector3& v)
    {
        return std::sqrt(v.x * v.x + v.z * v.z);
    }
} // namespace

// 外れの相手は着地のたびに跳ねる。水平の速さは残る割合だけ落ち、上向きの速さは水平の速さに比べて決まる
TEST(MissHop, HopLosesSpeedAndRisesInProportionToTheHorizontalSpeed)
{
    const NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};
    const NS::Core::Vector3 landing{0.0f, -3.0f, 10.0f};
    const NS::Core::Vector3 hop = MissHopVelocity(landing, up, Desc(), 7u, 0);
    EXPECT_NEAR(HorizontalSpeed(hop), 7.0f, 1.0e-4f);
    // 上向きは 水平の速さ × 高さの割合 × 0.5〜1
    EXPECT_GE(hop.y, 7.0f * 0.35f * 0.5f - 1.0e-4f);
    EXPECT_LE(hop.y, 7.0f * 0.35f + 1.0e-4f);
}

// 跳ねる向きはぶれの角度の中で左右に振れ、同じ種と回数なら同じ向きになる
TEST(MissHop, TurnStaysWithinTheAngleAndRepeatsForTheSameSeed)
{
    const NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};
    const NS::Core::Vector3 landing{0.0f, -3.0f, 10.0f};
    bool turnedLeft = false;
    bool turnedRight = false;
    for (int index = 0; index < 3; ++index)
    {
        for (unsigned seed = 1u; seed < 40u; ++seed)
        {
            const NS::Core::Vector3 hop = MissHopVelocity(landing, up, Desc(), seed, index);
            const float angle = std::atan2(hop.x, hop.z) * 180.0f / k_Pi;
            EXPECT_LE(std::abs(angle), 60.0f + 1.0e-3f);
            turnedLeft = turnedLeft || angle < -10.0f;
            turnedRight = turnedRight || angle > 10.0f;
            const NS::Core::Vector3 again = MissHopVelocity(landing, up, Desc(), seed, index);
            EXPECT_TRUE(hop == again);
        }
    }
    EXPECT_TRUE(turnedLeft);
    EXPECT_TRUE(turnedRight);
}

// 1 回目と 2 回目の跳ねは違う向きへ振れる。同じ向きへ曲がり続けると不規則に見えない
TEST(MissHop, SuccessiveHopsTurnDifferently)
{
    const NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};
    const NS::Core::Vector3 landing{0.0f, -3.0f, 10.0f};
    const NS::Core::Vector3 first = MissHopVelocity(landing, up, Desc(), 5u, 0);
    const NS::Core::Vector3 second = MissHopVelocity(landing, up, Desc(), 5u, 1);
    EXPECT_GT(std::abs(std::atan2(first.x, first.z) - std::atan2(second.x, second.z)), 0.05f);
}

// 水平に動いていない時は跳ねない
TEST(MissHop, NoHorizontalSpeedGivesNoHop)
{
    const NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};
    const NS::Core::Vector3 hop = MissHopVelocity(NS::Core::Vector3{0.0f, -3.0f, 0.0f}, up, Desc(), 3u, 0);
    EXPECT_TRUE(hop == NS::Core::Vector3(0.0f, 0.0f, 0.0f));
}
