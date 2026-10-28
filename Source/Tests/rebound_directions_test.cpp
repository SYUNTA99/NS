#include "Game/Level/ReboundDirections.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

// 反動の向きの表 (資料 v0.2 の 6.1・6.2) の作り方・割り当て・画面で見た入力補正・曲げの上限を縛る

namespace
{
    using GL::Level::AssignReboundDirection;
    using GL::Level::ChooseReboundDirection;
    using GL::Level::DirectionStepRadians;
    using GL::Level::MakeReboundDirections;
    using GL::Level::ReboundChoice;
    using GL::Level::ReboundChoiceLimits;
    using GL::Level::ReboundHit;
    using GL::Level::ReboundInputResult;
    using GL::Level::ReboundTableShape;
    using GL::Level::ReflectAcross;
    using GL::Level::ScreenAxes;
    using GL::Level::ShiftInputOnScreen;
    using GL::Level::TurnToward;

    constexpr float k_Tolerance = 1.0e-4f;
    const float k_Diagonal = std::sqrt(0.5f);

    void ExpectNear(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, k_Tolerance);
        EXPECT_NEAR(actual.y, expected.y, k_Tolerance);
        EXPECT_NEAR(actual.z, expected.z, k_Tolerance);
    }

    // 横から見るカメラ。画面の左は +Z、奥は +X
    ScreenAxes SideCamera()
    {
        return ScreenAxes{.right = NS::Vector3{0, 0, -1},
                          .up = NS::Vector3{0, 1, 0},
                          .forward = NS::Vector3{1, 0, 0},
                          .flatForward = NS::Vector3{1, 0, 0}};
    }

    // 後ろから 15° 見下ろすカメラ。奥は +Z
    ScreenAxes BehindCamera()
    {
        const float pitch = NS::k_Pi / 12.0f;
        return ScreenAxes{.right = NS::Vector3{1, 0, 0},
                          .up = NS::Vector3{0.0f, std::cos(pitch), std::sin(pitch)},
                          .forward = NS::Vector3{0.0f, -std::sin(pitch), std::cos(pitch)},
                          .flatForward = NS::Vector3{0, 0, 1}};
    }

    // 同じに見なす差 10°・奥と手前の余裕 10°・表は 8 分割
    ReboundChoiceLimits Limits()
    {
        return ReboundChoiceLimits{.screenTieRadians = NS::k_Pi / 18.0f,
                                   .depthSlackRadians = NS::k_Pi / 18.0f,
                                   .stepRadians = DirectionStepRadians(8)};
    }

    ReboundHit HitOn(std::span<const NS::Vector3> table, const NS::Vector3& incoming, const NS::Vector3& normal)
    {
        ReboundHit hit{.normal = normal, .incoming = incoming};
        EXPECT_TRUE(AssignReboundDirection(table, incoming, normal, hit.assigned));
        return hit;
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
            EXPECT_GT(NS::AngleBetweenUnit(table[i], table[j]), 0.01f) << i << " と " << j << " が重なる";
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
    EXPECT_GT(NS::AngleBetweenUnit(planes, corner), NS::AngleBetweenUnit(yawPitch, corner) + 0.1f);
}

TEST(ReboundDirectionsTest, NoInputKeepsTheAssignedDirection)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit = HitOn(table, NS::Vector3{0, 0, 1}, NS::Vector3{0, 0, -1});
    const ReboundChoice choice = ChooseReboundDirection(table, hit, SideCamera(), 0.0f, 0.0f, Limits());
    EXPECT_EQ(choice.result, ReboundInputResult::Kept);
    ExpectNear(choice.direction, hit.assigned);
}

TEST(ReboundDirectionsTest, SideViewInputMatchingTheAssignedDirectionKeepsIt)
{
    // 奥の左へ返る反射は、画面では真横の左と同じに見える。左を入力したら合ったとして、奥の左のまま強く飛ぶ
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit =
        HitOn(table, NS::Vector3{-k_Diagonal, 0.0f, -k_Diagonal}, NS::Vector3{k_Diagonal, 0.0f, k_Diagonal});
    ExpectNear(hit.assigned, NS::Vector3{k_Diagonal, 0.0f, k_Diagonal});
    const ReboundChoice choice = ChooseReboundDirection(table, hit, SideCamera(), -1.0f, 0.0f, Limits());
    EXPECT_EQ(choice.result, ReboundInputResult::Matched);
    ExpectNear(choice.direction, hit.assigned);
}

TEST(ReboundDirectionsTest, DepthNeverExceedsWhatTheDashHadBeforeTheHit)
{
    // 真横の左へ進んで斜めの壁に当たり、奥の右へ返る。1 段先の奥の右上は画面で上に近くても、当たる前に奥へ進んでいないので
    // 選ばない。ほかに上へ近づく 1 段先は無いので曲げない
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit = HitOn(table, NS::Vector3{0, 0, 1}, NS::Vector3{0.5f, 0.0f, -std::sqrt(0.75f)});
    ExpectNear(hit.assigned, NS::Vector3{k_Diagonal, 0.0f, -k_Diagonal});
    const ReboundChoice choice = ChooseReboundDirection(table, hit, SideCamera(), 0.0f, 1.0f, Limits());
    EXPECT_EQ(choice.result, ReboundInputResult::Kept);
    ExpectNear(choice.direction, hit.assigned);
}

TEST(ReboundDirectionsTest, DepthLimitAllowsASlightlyTurnedCamera)
{
    // 横から見るカメラが 0.5° 回り、当たる前の向きも同じだけずれていて、当たる前の奥の成分が 0 になる。
    // 正面の壁で画面の右へ返る所に上を入力し、1 段先の右上を選ぶ。カメラのずれで右上の奥の成分が 0 を超えても外さない
    const float yaw = NS::k_Pi / 360.0f;
    const ScreenAxes camera{.right = NS::Vector3{-std::sin(yaw), 0.0f, -std::cos(yaw)},
                            .up = NS::Vector3{0, 1, 0},
                            .forward = NS::Vector3{std::cos(yaw), 0.0f, -std::sin(yaw)},
                            .flatForward = NS::Vector3{std::cos(yaw), 0.0f, -std::sin(yaw)}};
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit = HitOn(table, NS::Vector3{std::sin(yaw), 0.0f, std::cos(yaw)}, NS::Vector3{0, 0, -1});
    ExpectNear(hit.assigned, NS::Vector3{0, 0, -1});
    const ReboundChoice choice = ChooseReboundDirection(table, hit, camera, 0.0f, 1.0f, Limits());
    EXPECT_EQ(choice.result, ReboundInputResult::Shifted);
    ExpectNear(choice.direction, NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
}

TEST(ReboundDirectionsTest, InputOffTheAssignedDirectionTurnsOnlyOneStep)
{
    // 45° の坂で真上へ 90° 曲がって返る所に右を入力すると、1 段先の右上へ曲がる。2 段先の右の真横へは行かない
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit = HitOn(table, NS::Vector3{0, 0, 1}, NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
    const ReboundChoice side = ChooseReboundDirection(table, hit, SideCamera(), 1.0f, 0.0f, Limits());
    EXPECT_EQ(side.result, ReboundInputResult::Shifted);
    ExpectNear(side.direction, NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
    const ReboundChoice behind = ChooseReboundDirection(table, hit, BehindCamera(), 1.0f, 0.0f, Limits());
    EXPECT_EQ(behind.result, ReboundInputResult::Shifted);
    ExpectNear(behind.direction, NS::Vector3{k_Diagonal, k_Diagonal, 0.0f});
}

TEST(ReboundDirectionsTest, StraightUpWithUpInputStaysStraightUpFromEveryView)
{
    // 45° の坂へ水平に当てて真上へ返る所に上を入力すると、どの視点でも真上のまま合う。後ろから見ると上奥は坂と平行で使えず、
    // 前は上左奥と上右奥が同じだけ奥へ進み、浮動小数の最後の桁で左へ曲がった
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit = HitOn(table, NS::Vector3{0, 0, 1}, NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
    ExpectNear(hit.assigned, NS::Vector3{0, 1, 0});
    for (const ScreenAxes& screen : {BehindCamera(), SideCamera()})
    {
        const ReboundChoice choice = ChooseReboundDirection(table, hit, screen, 0.0f, 1.0f, Limits());
        EXPECT_EQ(choice.result, ReboundInputResult::Matched);
        ExpectNear(choice.direction, NS::Vector3{0, 1, 0});
    }
}

TEST(ReboundDirectionsTest, TwoSameLookingStepsOnBothSidesKeepTheAssignedDirection)
{
    // 真上へ飛ぶ所に後ろを入力する。1 段以内で画面の下に一番近いのは右上と左上で、同じだけ近いので曲げない
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const ReboundHit hit = HitOn(table, NS::Vector3{0, 0, 1}, NS::Vector3{0.0f, k_Diagonal, -k_Diagonal});
    const ReboundChoice choice = ChooseReboundDirection(table, hit, BehindCamera(), 0.0f, -1.0f, Limits());
    EXPECT_EQ(choice.result, ReboundInputResult::Kept);
    ExpectNear(choice.direction, NS::Vector3{0, 1, 0});
}

TEST(ReboundDirectionsTest, ScreenUpTargetsUpOffAFrontWallWithALevelCamera)
{
    const NS::Vector3 back{0.0f, 0.0f, -1.0f};
    ExpectNear(ShiftInputOnScreen(back, NS::Vector3{1, 0, 0}, NS::Vector3{0, 1, 0}, NS::Vector3{0, 0, 1}, 0.0f, 1.0f),
               NS::Vector3{0, 1, 0});
}

TEST(ReboundDirectionsTest, ScreenUpTargetsIntoTheScreenWhenGoingStraightUp)
{
    const NS::Vector3 up{0.0f, 1.0f, 0.0f};
    ExpectNear(ShiftInputOnScreen(up, NS::Vector3{1, 0, 0}, NS::Vector3{0, 1, 0}, NS::Vector3{0, 0, 1}, 0.0f, 1.0f),
               NS::Vector3{0, 0, 1});
}

TEST(ReboundDirectionsTest, ScreenUpFollowsASteepCamera)
{
    // 60° 見下ろすカメラでは画面の上が水平に近く、横へ進む時の上は世界の上でなく画面の奥側を向く
    const NS::Vector3 side{1.0f, 0.0f, 0.0f};
    const float pitch = NS::k_Pi / 3.0f;
    const NS::Vector3 forward{0.0f, -std::sin(pitch), std::cos(pitch)};
    const NS::Vector3 screenUp{0.0f, std::cos(pitch), std::sin(pitch)};
    ExpectNear(ShiftInputOnScreen(side, NS::Vector3{1, 0, 0}, screenUp, forward, 0.0f, 1.0f), screenUp);
}

TEST(ReboundDirectionsTest, TurnTowardStopsAtTheGivenAngle)
{
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    const NS::Vector3 right{1.0f, 0.0f, 0.0f};
    const float limit = NS::k_Pi / 18.0f;
    const NS::Vector3 turned = TurnToward(forward, right, limit);
    EXPECT_NEAR(NS::AngleBetweenUnit(turned, forward), limit, k_Tolerance);
    EXPECT_NEAR(turned.Length(), 1.0f, k_Tolerance);
    // 上限より近ければ、目標の向きで止まる
    ExpectNear(TurnToward(forward, right, NS::k_Pi), right);
}

// D-08: 45° の物に当たって 90° 曲がる時は、曲がった向きが画面に見える向きへ入力すると、合ったとして強く飛ぶ。
// 前は奥と手前の上限が曲がった向き自身を候補から外し、同じに見える向きが並ぶと別の向きを選んで、21 組で外れた。
// 物の向き (曲がる先を進む向きの周りに 45° ずつ) × カメラの水平の向き (45° ずつ、15° 見下ろす) の全部の組で見る
TEST(ReboundDirectionsTest, InputTowardTheNinetyDegreeTurnIsMatchedForEveryFortyFiveDegreeFace)
{
    const std::vector<NS::Vector3> table = MakeReboundDirections(8, ReboundTableShape::YawPitch);
    const NS::Vector3 incoming{0, 0, 1};
    const NS::Vector3 worldUp{0, 1, 0};
    const NS::Vector3 side{1, 0, 0};
    const float pitch = NS::k_Pi / 12.0f;
    for (int camera = 0; camera < 8; ++camera)
    {
        const float yaw = NS::k_Pi / 4.0f * static_cast<float>(camera);
        const NS::Vector3 flatForward{std::sin(yaw), 0.0f, std::cos(yaw)};
        const NS::Vector3 right{flatForward.z, 0.0f, -flatForward.x};
        NS::Vector3 forward = flatForward * std::cos(pitch) - worldUp * std::sin(pitch);
        forward.Normalize();
        NS::Vector3 up = NS::Cross(forward, right);
        up.Normalize();
        const ScreenAxes screen{.right = right, .up = up, .forward = forward, .flatForward = flatForward};
        for (int face = 0; face < 8; ++face)
        {
            // 曲がる先は進む向きに垂直な向き。物の法線は、進む向きの逆と曲がる先の間
            const float around = NS::k_Pi / 4.0f * static_cast<float>(face);
            const NS::Vector3 turn = side * std::cos(around) + worldUp * std::sin(around);
            NS::Vector3 normal = turn - incoming;
            normal.Normalize();
            const ReboundHit hit = HitOn(table, incoming, normal);
            // 本人は曲がった向きが画面のどちらに見えるかを見て、スティックをその向きへ入れる
            const float onRight = NS::Dot(hit.assigned, screen.right);
            const float onUp = NS::Dot(hit.assigned, screen.up);
            if (NS::IsNearlyZero(std::sqrt(onRight * onRight + onUp * onUp)))
            {
                continue;
            }
            const ReboundChoice choice = ChooseReboundDirection(table, hit, screen, onRight, onUp, Limits());
            const bool matched = choice.result == ReboundInputResult::Matched;
            EXPECT_TRUE(matched) << "カメラ " << camera * 45 << "° / 物 " << face * 45 << "° / 曲がる先 ("
                                 << hit.assigned.x << ", " << hit.assigned.y << ", " << hit.assigned.z
                                 << ") / 選んだ向き (" << choice.direction.x << ", " << choice.direction.y << ", "
                                 << choice.direction.z << ") / 結果 " << static_cast<int>(choice.result);
        }
    }
}
