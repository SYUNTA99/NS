#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Player.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// 溜めている間の矢印が、放った玉の実際に通る道筋に高さ込みで沿うか (R-10)
// 放つ上下の速さを狙いの段で控えて放す時に添えるか (R-7・R-8)

namespace
{
    using NS::Core::Vector3;

    // 床の上面 y = 0 に自機と、+z の先に相手を置く。カメラは +z を向く
    Player* PlaceOnFloor(NS::Obj::Scene& scene,
                         const Vector3& playerPosition,
                         bool withTarget,
                         float targetScale,
                         const Vector3& targetPosition,
                         const nlohmann::json& targetParts = nlohmann::json::object())
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, playerPosition);
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        if (withTarget)
        {
            nlohmann::json rock = NS::Obj::MakeObjectJson();
            NS::Obj::SetObjectJsonClass(rock, "MapObj");
            NS::Obj::SetObjectJsonId(rock, 2);
            NS::Obj::SetObjectPosition(rock, targetPosition);
            NS::Obj::SetObjectScale(rock, Vector3{targetScale, targetScale, targetScale});
            for (nlohmann::json::const_iterator it = targetParts.begin(); it != targetParts.end(); ++it)
            {
                NS::Obj::ObjectJsonParts(rock)[it.key()] = it.value();
            }
            NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        }
        scene.LoadJson(doc);
        NS::Core::OBB floor{};
        floor.center = Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, Vector3{0.0f, playerPosition.y, -1.0f}, Vector3{0.0f, playerPosition.y, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    // 線に沿った距離 along での帯の高さ。板の中は近い端から遠い端へ直線で上がる
    bool BandHeightAt(const NS::Game::Level::SlamArrowShape& arrow, float along, float& outHeight)
    {
        for (const NS::Game::Level::SlamArrowPiece& piece : arrow.band)
        {
            if (along < piece.alongNear || along > piece.alongFar)
            {
                continue;
            }
            const float t = (along - piece.alongNear) / (piece.alongFar - piece.alongNear);
            outHeight = piece.height + piece.rise * t;
            return true;
        }
        return false;
    }

    constexpr int k_ChargeFrames = 70;
} // namespace

// 背の高い相手へ地上から放つと上向きの弧で出る。矢印はその弧を玉の一番下の点の高さでなぞる
TEST(SlamArrowTest, ArcArrowFollowsTheBallThatIsActuallyLaunched)
{
    NS::Obj::Scene scene;
    Player* player = PlaceOnFloor(scene, Vector3{0.0f, 1.0f, 0.0f}, true, 3.0f, Vector3{0.0f, 1.5f, 8.0f});
    ASSERT_NE(player, nullptr);
    for (int frame = 0; frame < k_ChargeFrames; ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    NS::Game::Level::SlamLineTarget target{};
    ASSERT_TRUE(player->TryGetAimTarget(target));
    EXPECT_GT(target.launchVerticalSpeed, 0.0f);
    NS::Game::Level::SlamArrowShape arrow{};
    ASSERT_TRUE(player->SlamIndicator().TryGetShownArrow(arrow));
    ASSERT_FALSE(arrow.band.empty());

    const float radius = player->Collider().CapsuleRadius();
    const float lift = NS::Game::Level::SlamArrowDesc{}.groundLift;
    const float startHeight = player->Root().Position().y;
    float highest = startHeight;
    int checked = 0;
    for (int frame = 0; frame < 60; ++frame)
    {
        player->Update(false);
        if (frame == 0)
        {
            // 放した瞬間の縦の速さは狙いの段で控えた値。同じフレームの重力を 1 回当てた後を読む
            ASSERT_TRUE(player->IsBodySlamming());
            const float dt = NS::Platform::FrameTimer::FixedDelta();
            const NS::Game::Player::PlayerGravity gravity = player->Params().Gravity();
            EXPECT_NEAR(player->Body().VerticalVelocity(),
                        target.launchVerticalSpeed +
                            NS::Game::Player::ChooseGravity(gravity, target.launchVerticalSpeed) * dt,
                        0.0001f);
        }
        if (player->Resolver().LastImpact().sequence > 0 || !player->IsBodySlamming())
        {
            break;
        }
        const Vector3 position = player->Root().Position();
        highest = std::max(highest, position.y);
        const float along = (position - arrow.origin).Dot(arrow.direction);
        float band = 0.0f;
        if (along < arrow.start || along > arrow.tip - arrow.headDepth || !BandHeightAt(arrow, along, band))
        {
            continue;
        }
        SCOPED_TRACE(frame);
        EXPECT_NEAR(band, position.y - radius + lift, 0.01f);
        ++checked;
    }
    EXPECT_GT(checked, 3);
    // 弧で上がった。床に沿うまっすぐな矢印とは違う形
    EXPECT_GT(highest, startHeight + 0.3f);
    EXPECT_GT(player->Resolver().LastImpact().sequence, 0u);
}

// 予測の段は、放つ上下の速さで計算した弧の、相手に触れる所の高さで出す
// 赤を上へずらした背の高い相手は、今の玉の高さの線では外れでも、弧で赤に着くので真ん中
TEST(SlamArrowTest, PredictedTierUsesTheArcHeightAtContact)
{
    NS::Obj::Scene scene;
    const nlohmann::json raised{{"HitZones", {{"上下の位置", 0.5f}, {"縦の幅", 0.2f}}}};
    Player* player = PlaceOnFloor(scene, Vector3{0.0f, 1.0f, 0.0f}, true, 3.0f, Vector3{0.0f, 1.5f, 8.0f}, raised);
    ASSERT_NE(player, nullptr);
    for (int frame = 0; frame < 20; ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    NS::Game::Level::SlamLineTarget target{};
    ASSERT_TRUE(player->TryGetAimTarget(target));
    EXPECT_GT(target.launchVerticalSpeed, 0.0f);
    EXPECT_EQ(target.tier, NS::Game::Level::HitTier::Center);
}

// 相手が無くても矢印を出す。地上では今と同じく床に沿うまっすぐな形で、突進が止まる所まで伸びる
TEST(SlamArrowTest, GroundArrowWithoutTargetLiesOnTheFloor)
{
    NS::Obj::Scene scene;
    Player* player = PlaceOnFloor(scene, Vector3{0.0f, 1.0f, 0.0f}, false, 1.0f, Vector3{});
    ASSERT_NE(player, nullptr);
    for (int frame = 0; frame < k_ChargeFrames; ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    NS::Game::Level::SlamArrowShape arrow{};
    ASSERT_TRUE(player->SlamIndicator().TryGetShownArrow(arrow));
    ASSERT_FALSE(arrow.band.empty());
    EXPECT_NEAR(arrow.fullTip, player->BodySlamDistance() + player->Collider().CapsuleRadius(), 0.0001f);
    const float lift = NS::Game::Level::SlamArrowDesc{}.groundLift;
    for (const NS::Game::Level::SlamArrowPiece& piece : arrow.band)
    {
        EXPECT_NEAR(piece.height, lift, 0.01f);
        EXPECT_FLOAT_EQ(piece.rise, 0.0f);
    }
}

// 空中で相手が無い時は、縦の速さ 0 で水平に出て落ちる道筋に矢印を描く
// 上がっている途中に放しても、放った瞬間の縦の速さは 0 から
TEST(SlamArrowTest, AirArrowWithoutTargetFallsAndTheReleaseStartsLevel)
{
    NS::Obj::Scene scene;
    Player* player = PlaceOnFloor(scene, Vector3{0.0f, 6.0f, 0.0f}, false, 1.0f, Vector3{});
    ASSERT_NE(player, nullptr);
    player->Body().SetVerticalVelocity(10.0f);
    for (int frame = 0; frame < 15; ++frame)
    {
        player->Update(true);
    }
    ASSERT_FALSE(player->Body().IsGrounded());
    ASSERT_GT(player->Body().VerticalVelocity(), 1.0f);
    NS::Game::Level::SlamArrowShape arrow{};
    ASSERT_TRUE(player->SlamIndicator().TryGetShownArrow(arrow));
    ASSERT_GE(arrow.band.size(), 2u);
    EXPECT_LT(arrow.band.back().height + arrow.band.back().rise, arrow.band.front().height - 0.5f);

    player->Update(false);
    ASSERT_TRUE(player->IsBodySlamming());
    const float dt = NS::Platform::FrameTimer::FixedDelta();
    EXPECT_NEAR(player->Body().VerticalVelocity(),
                NS::Game::Player::ChooseGravity(player->Params().Gravity(), 0.0f) * dt,
                0.0001f);
}

// 溜めきりの後は、溜めすぎの深さで溜めきりの赤から溜めすぎの紫へ移る。赤のうちと溜めきる前は今の色のまま
TEST(SlamArrowTest, FullArrowTurnsFromRedToPurpleWithTheOvercharge)
{
    NS::Game::Level::SlamArrowDesc desc{};
    NS::Game::Level::SlamArrowState state{};
    state.line.origin = Vector3{};
    state.line.direction = Vector3{0.0f, 0.0f, 1.0f};
    state.line.length = 10.0f;
    state.ballRadius = 0.5f;
    state.charge01 = 1.0f;
    state.chargeFull = true;
    NS::Game::Level::SlamArrowShape shape{};
    ASSERT_TRUE(NS::Game::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_TRUE(shape.stageColor == desc.fullColor);

    state.overcharge01 = 1.0f;
    ASSERT_TRUE(NS::Game::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_TRUE(shape.stageColor == desc.overchargeColor);

    state.overcharge01 = 0.5f;
    ASSERT_TRUE(NS::Game::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_FLOAT_EQ(shape.stageColor.x, (desc.fullColor.x + desc.overchargeColor.x) * 0.5f);
    EXPECT_FLOAT_EQ(shape.stageColor.y, (desc.fullColor.y + desc.overchargeColor.y) * 0.5f);
    EXPECT_FLOAT_EQ(shape.stageColor.z, (desc.fullColor.z + desc.overchargeColor.z) * 0.5f);

    // 溜めきる前に溜めすぎの深さが入っていても紫にしない
    state.chargeFull = false;
    state.charge01 = 0.9f;
    ASSERT_TRUE(NS::Game::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_TRUE(shape.stageColor == desc.lateColor);
}

namespace
{
    // 床 (y = 0) に貼った +z へ向かう矢印を、framesSinceShown フレーム目の長さで組んで描く単位にする
    std::vector<NS::Gfx::DrawItem> DrawGroundArrow(const NS::Game::Level::SlamArrowDesc& desc,
                                                   int framesSinceShown,
                                                   const Vector3& cameraPosition,
                                                   NS::Game::Level::SlamArrowShape& outShape)
    {
        NS::Game::Level::SlamArrowState state{};
        state.line.origin = Vector3{0.0f, 0.5f, 0.0f};
        state.line.direction = Vector3{0.0f, 0.0f, 1.0f};
        state.line.length = 10.0f;
        state.line.grounded = true;
        state.ballRadius = 0.5f;
        state.charge01 = 0.1f;
        state.framesSinceShown = framesSinceShown;
        EXPECT_TRUE(NS::Game::Level::BuildSlamArrow(state, desc, outShape));
        const NS::Game::Level::SlamArrowGroundProbe flat = [](const Vector3&, float, float& outGroundY) {
            outGroundY = 0.0f;
            return true;
        };
        NS::Game::Level::PlaceSlamArrowOnGround(flat, desc.groundLift, outShape);
        EXPECT_TRUE(outShape.hasHead);
        std::vector<NS::Gfx::DrawItem> items;
        NS::Game::Level::AppendSlamArrowDrawItems(
            outShape, desc, NS::Core::Matrix::Identity, cameraPosition, NS::Game::Level::SlamArrowDrawAssets{}, items);
        return items;
    }

    // 溜め始めの床の矢印を、出したフレームの短い長さのまま真上の高いカメラから描く
    std::vector<NS::Gfx::DrawItem> DrawEarlyGroundArrow(const NS::Game::Level::SlamArrowDesc& desc)
    {
        NS::Game::Level::SlamArrowShape shape{};
        return DrawGroundArrow(desc, 0, Vector3{0.0f, 40.0f, -6.0f}, shape);
    }

    NS::Game::Level::SlamArrowConstants ConstantsOf(const NS::Gfx::DrawItem& item)
    {
        NS::Game::Level::SlamArrowConstants constants{};
        std::memcpy(&constants, &item.constants, sizeof(constants));
        return constants;
    }
} // namespace

// 帯と矢じりの板は裏からも描く。下る道筋の板はカメラから裏を向き、裏を描かないと帯の先と矢じりが丸ごと消える
TEST(SlamArrowTest, EveryPlateIsDrawnFromBothSides)
{
    const std::vector<NS::Gfx::DrawItem> items = DrawEarlyGroundArrow(NS::Game::Level::SlamArrowDesc{});
    ASSERT_GE(items.size(), 3u);
    for (const NS::Gfx::DrawItem& item : items)
    {
        EXPECT_TRUE(item.twoSided);
    }
}

// 矢じりは溜めの始めから段の色で塗り、始まりのぼかしも掛けない。白っぽい薄い塗りだと市松の床に溶け、
// 出たばかりの短い矢印では玉の縁のぼかしに矢じりまで消える。溜め量を示す色の先は帯だけが持つ
TEST(SlamArrowTest, HeadIsColoredAndUnfadedFromTheStartOfTheCharge)
{
    const std::vector<NS::Gfx::DrawItem> items = DrawEarlyGroundArrow(NS::Game::Level::SlamArrowDesc{});
    ASSERT_GE(items.size(), 3u);
    // 後ろの 2 つが矢じりと、隠れた所へ描く矢じり
    for (std::size_t i = items.size() - 2; i < items.size(); ++i)
    {
        SCOPED_TRACE(i);
        const NS::Game::Level::SlamArrowConstants head = ConstantsOf(items[i]);
        EXPECT_FLOAT_EQ(head.fadeAndFront.x, 1.0f);
        EXPECT_FLOAT_EQ(head.fadeAndFront.y, 0.0f);
        EXPECT_FLOAT_EQ(head.fadeAndFront.z, 1.0f);
        EXPECT_FLOAT_EQ(head.fadeAndFront.w, 0.0f);
    }
}

// カメラが低く、床の矢じりを見る角度が欄「矢じりを見せる最小の角度」を下回る時は、矢じりの板を手前の端を軸に
// カメラの方へ起こし、手前の端をその角度で見せる。起こしても板の長さは変えない。カメラが高ければ床に寝たまま
TEST(SlamArrowTest, LowCameraStandsTheHeadUpToTheMinimumViewAngle)
{
    const NS::Game::Level::SlamArrowDesc desc{};
    const Vector3 high{0.0f, 40.0f, -6.0f};
    const Vector3 low{0.0f, 1.0f, -6.0f};
    NS::Game::Level::SlamArrowShape shape{};
    const std::vector<NS::Gfx::DrawItem> lying = DrawGroundArrow(desc, 30, high, shape);
    const std::vector<NS::Gfx::DrawItem> standing = DrawGroundArrow(desc, 30, low, shape);
    ASSERT_GE(lying.size(), 3u);
    ASSERT_EQ(lying.size(), standing.size());
    // 起こす軸の、矢じりの手前の端
    const Vector3 pivot =
        shape.origin + shape.direction * shape.head.alongNear + Vector3{0.0f, shape.head.height - shape.origin.y, 0.0f};
    for (std::size_t i = lying.size() - 2; i < lying.size(); ++i)
    {
        SCOPED_TRACE(i);
        const NS::Core::Matrix flat = ConstantsOf(lying[i]).world;
        const NS::Core::Matrix raised = ConstantsOf(standing[i]).world;
        EXPECT_FLOAT_EQ(flat._32, 0.0f);
        const Vector3 lengthAxis{raised._31, raised._32, raised._33};
        const Vector3 widthAxis{raised._11, raised._12, raised._13};
        const Vector3 lyingLengthAxis{flat._31, flat._32, flat._33};
        EXPECT_NEAR(lengthAxis.Length(), lyingLengthAxis.Length(), 1.0e-4f);
        Vector3 normal = lengthAxis.Cross(widthAxis);
        normal.Normalize();
        Vector3 toCamera = low - pivot;
        toCamera.Normalize();
        const float viewDegrees = NS::Core::RadiansToDegrees(std::asin(std::abs(normal.Dot(toCamera))));
        EXPECT_NEAR(viewDegrees, desc.headMinViewDegrees, 0.1f);
    }
    // 帯は床に寝たまま
    EXPECT_FLOAT_EQ(ConstantsOf(standing.front()).world._32, 0.0f);
}
