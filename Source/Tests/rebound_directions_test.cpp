#include "Game/Level/ReboundDirections.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

// 反動の向きの表 (資料 v0.2 の 6.1・6.2) の作り方・割り当て・隣へのずらし・曲げの上限を縛る

namespace
{
    using GL::Level::AssignReboundDirection;
    using GL::Level::DirectionStepRadians;
    using GL::Level::MakeReboundDirections;
    using GL::Level::ReboundTableShape;
    using GL::Level::ReflectAcross;
    using GL::Level::ShiftInputOnScreen;
    using GL::Level::ShiftReboundDirection;
    using GL::Level::TurnToward;

    constexpr float k_Tolerance = 1.0e-4f;
    const float k_Diagonal = std::sqrt(0.5f);

    void ExpectNear(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, k_Tolerance);
        EXPECT_NEAR(actual.y, expected.y, k_Tolerance);
        EXPECT_NEAR(actual.z, expected.z, k_Tolerance);
    }

    float AngleBetween(const NS::Vector3& a, const NS::Vector3& b)
    {
        return std::acos(std::clamp(NS::Dot(a, b), -1.0f, 1.0f));
    }
} // namespace

TEST(ReboundDirectionsTest, CountsMatchTheDocumentForEachDivisionAndShape)
{
    // 資料の表: 8 のとき ① は 18 方向、② は 26 方向
    EXPECT_EQ(MakeReboundDirections(8, ReboundTableShape::ThreePlanes).size(), 18u);
    EXPECT_EQ(MakeReboundDirections(8, ReboundTableShape::YawPitch).size(), 26u);
    // ① は 3 枚の円で軸の 6 方向を共有する。② は傾きの段 × 水平の分割と、真上と真下
    EXPECT_EQ(MakeReboundDirections(12, ReboundTableShape::ThreePlanes).size(), 30u);
    EXPECT_EQ(MakeReboundDirections(16, ReboundTableShape::ThreePlanes).size(), 42u);
    EXPECT_EQ(MakeReboundDirections(12, ReboundTableShape::YawPitch).size(), 62u);
    EXPECT_EQ(MakeReboundDirections(16, ReboundTableShape::YawPitch).size(), 114u);
}

TEST(ReboundDirectionsTest, EveryDirectionIsUnitLengthAndDistinct)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        EXPECT_NEAR(table[i].Length(), 1.0f, k_Tolerance);
        for (std::size_t j = i + 1; j < table.size(); ++j)
        {
            EXPECT_GT(AngleBetween(table[i], table[j]), 0.01f) << i << " と " << j << " が重なる";
        }
    }
}

TEST(ReboundDirectionsTest, UnsupportedDivisionsSnapToTheNearestSupportedOne)
{
    EXPECT_EQ(MakeReboundDirections(9, ReboundTableShape::YawPitch).size(), 26u);
    EXPECT_EQ(MakeReboundDirections(0, ReboundTableShape::YawPitch).size(), 26u);
    EXPECT_EQ(MakeReboundDirections(100, ReboundTableShape::YawPitch).size(), 114u);
    EXPECT_NEAR(DirectionStepRadians(8), NS::k_Pi / 4.0f, k_Tolerance);
    EXPECT_NEAR(DirectionStepRadians(13), NS::k_Pi / 6.0f, k_Tolerance);
}

TEST(ReboundDirectionsTest, ReflectionFlipsOnlyTheNormalComponent)
{
    // 床に斜め前下へ当たると、前へ進む成分は残って斜め前上へ (資料 8)
    const NS::Vector3 reflected = ReflectAcross(NS::Vector3{0.0f, -k_Diagonal, k_Diagonal}, NS::Vector3{0, 1, 0});
    ExpectNear(reflected, NS::Vector3{0.0f, k_Diagonal, k_Diagonal});
}

TEST(ReboundDirectionsTest, FrontWallSendsStraightBack)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 normal{0.0f, 0.0f, -1.0f};
    NS::Vector3 assigned{};
    ASSERT_TRUE(AssignReboundDirection(table, NS::Vector3{0, 0, 1}, normal, assigned));
    ExpectNear(assigned, NS::Vector3{0.0f, 0.0f, -1.0f});
}

TEST(ReboundDirectionsTest, ShallowSideWallHitSnapsAwayFromTheWall)
{
    // 右の壁 (法線 −X) へ 10° で当たる。反射は壁に沿う向き (0°) に一番近いが、面から離れないので候補に入らず、表の 45° へ丸まる
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const float angle = NS::k_Pi / 18.0f;
    const NS::Vector3 dash{std::sin(angle), 0.0f, std::cos(angle)};
    const NS::Vector3 normal{-1.0f, 0.0f, 0.0f};
    NS::Vector3 assigned{};
    ASSERT_TRUE(AssignReboundDirection(table, dash, normal, assigned));
    ExpectNear(assigned, NS::Vector3{-k_Diagonal, 0.0f, k_Diagonal});
    // 30° で当たっても同じ 45° へ丸まる
    const float wide = NS::k_Pi / 6.0f;
    ASSERT_TRUE(AssignReboundDirection(table, NS::Vector3{std::sin(wide), 0.0f, std::cos(wide)}, normal, assigned));
    ExpectNear(assigned, NS::Vector3{-k_Diagonal, 0.0f, k_Diagonal});
}

TEST(ReboundDirectionsTest, HorizontalDashIntoAFortyFiveDegreeSlopeGoesStraightUp)
{
    // +Z へ上る 45° の坂。水平に当たると物理の反射は真上
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 normal{0.0f, k_Diagonal, -k_Diagonal};
    NS::Vector3 assigned{};
    ASSERT_TRUE(AssignReboundDirection(table, NS::Vector3{0, 0, 1}, normal, assigned));
    ExpectNear(assigned, NS::Vector3{0.0f, 1.0f, 0.0f});
}

TEST(ReboundDirectionsTest, ThreePlaneTableMissesTheCornerThatYawPitchHas)
{
    // ① は斜めの坂に斜めに当たると物理の向きから大きく外れる (資料 6.1 の短所)。② は立方体の角の向きを持つ
    const NS::Vector3 corner = NS::Vector3{1.0f, 1.0f, 1.0f} / std::sqrt(3.0f);
    const NS::Vector3 normal = corner;
    const NS::Vector3 dash = -corner;
    NS::Vector3 planes{};
    NS::Vector3 yawPitch{};
    ASSERT_TRUE(AssignReboundDirection(MakeReboundDirections(8, ReboundTableShape::ThreePlanes), dash, normal, planes));
    ASSERT_TRUE(AssignReboundDirection(MakeReboundDirections(8, ReboundTableShape::YawPitch), dash, normal, yawPitch));
    EXPECT_GT(AngleBetween(planes, corner), AngleBetween(yawPitch, corner) + 0.1f);
}

TEST(ReboundDirectionsTest, StickUpShiftsOneStepUpFromStraightBack)
{
    // 資料 6.2 の例: 正面の壁に当たって上へ倒すと後ろ上 45°、右へ倒すと右後ろ 45°
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 normal{0.0f, 0.0f, -1.0f};
    const NS::Vector3 back{0.0f, 0.0f, -1.0f};
    const float step = DirectionStepRadians(8);
    ExpectNear(ShiftReboundDirection(table, back, normal, NS::Vector3{0, 1, 0}, step),
               NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
    ExpectNear(ShiftReboundDirection(table, back, normal, NS::Vector3{1, 0, 0}, step),
               NS::Vector3{k_Diagonal, 0.0f, -k_Diagonal});
}

TEST(ReboundDirectionsTest, NoStickKeepsTheAssignedDirection)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 back{0.0f, 0.0f, -1.0f};
    ExpectNear(ShiftReboundDirection(table, back, back, NS::Vector3{}, DirectionStepRadians(8)), back);
}

TEST(ReboundDirectionsTest, ShiftNeverGoesIntoTheSurfaceNorMoreThanOneStep)
{
    // 床から真上へ返った時に下へ倒しても、床へ向かう向きへはずらさない
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 up{0.0f, 1.0f, 0.0f};
    const float step = DirectionStepRadians(8);
    const NS::Vector3 shifted = ShiftReboundDirection(table, up, up, NS::Vector3{0, -1, 0}, step);
    EXPECT_GT(NS::Dot(shifted, up), 0.0f);
    EXPECT_LE(AngleBetween(shifted, up), step + k_Tolerance);
}

TEST(ReboundDirectionsTest, ScreenUpShiftsUpOffAFrontWallWithALevelCamera)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 back{0.0f, 0.0f, -1.0f};
    const NS::Vector3 shift = ShiftInputOnScreen(back, NS::Vector3{1, 0, 0}, NS::Vector3{0, 1, 0}, NS::Vector3{0, 0, 1}, 0.0f, 1.0f);
    ExpectNear(ShiftReboundDirection(table, back, back, shift, DirectionStepRadians(8)),
               NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
}

TEST(ReboundDirectionsTest, ScreenUpShiftsIntoTheScreenWhenBouncingStraightUp)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 up{0.0f, 1.0f, 0.0f};
    const NS::Vector3 shift = ShiftInputOnScreen(up, NS::Vector3{1, 0, 0}, NS::Vector3{0, 1, 0}, NS::Vector3{0, 0, 1}, 0.0f, 1.0f);
    ExpectNear(ShiftReboundDirection(table, up, up, shift, DirectionStepRadians(8)),
               NS::Vector3{0.0f, k_Diagonal, k_Diagonal});
}

TEST(ReboundDirectionsTest, ScreenUpFollowsASteepCameraOffASideWall)
{
    // 60° 見下ろすカメラでは画面の上が水平に近く、側面から返る向きは世界の上でなく画面の奥側へずれる
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 side{1.0f, 0.0f, 0.0f};
    const float pitch = NS::k_Pi / 3.0f;
    const NS::Vector3 forward{0.0f, -std::sin(pitch), std::cos(pitch)};
    const NS::Vector3 screenUp{0.0f, std::cos(pitch), std::sin(pitch)};
    const NS::Vector3 shift = ShiftInputOnScreen(side, NS::Vector3{1, 0, 0}, screenUp, forward, 0.0f, 1.0f);
    ExpectNear(ShiftReboundDirection(table, side, side, shift, DirectionStepRadians(8)),
               NS::Vector3{k_Diagonal, 0.0f, k_Diagonal});
}

TEST(ReboundDirectionsTest, TurnTowardStopsAtTheGivenAngle)
{
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    const NS::Vector3 right{1.0f, 0.0f, 0.0f};
    const float limit = NS::k_Pi / 18.0f;
    const NS::Vector3 turned = TurnToward(forward, right, limit);
    EXPECT_NEAR(AngleBetween(turned, forward), limit, k_Tolerance);
    EXPECT_NEAR(turned.Length(), 1.0f, k_Tolerance);
    // 上限より近ければ、目標の向きで止まる
    ExpectNear(TurnToward(forward, right, NS::k_Pi), right);
}
