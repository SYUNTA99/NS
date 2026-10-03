#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Player.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"

#include <gtest/gtest.h>

#include <algorithm>

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
        scene.MainCamera()->SetPosition(Vector3{0.0f, playerPosition.y, -1.0f});
        scene.MainCamera()->SetTarget(Vector3{0.0f, playerPosition.y, 1.0f});
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

    const float radius = player->Body().CapsuleRadius();
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
    EXPECT_NEAR(arrow.fullTip, player->BodySlamDistance() + player->Body().CapsuleRadius(), 0.0001f);
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
