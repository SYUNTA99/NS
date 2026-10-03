#include "Game/Level/CourseDirector.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"
#include "Tests/TestHitTimelines.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace
{
    Player* PlacePipelinePlayer(NS::Obj::Scene& scene, float targetX = 0.0f, float targetZ = 3.0f)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{targetX, 0.5f, targetZ});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        NS::Core::OBB floor{};
        floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Core::Vector3{}, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    // 見本の列に書く状態の綴り。表に無い状態は Other
    std::string_view StateName(const Player& player)
    {
        if (player.States().IsCurrent<NS::Game::Player::IdlePlayerState>())
        {
            return "Idle";
        }
        if (player.States().IsCurrent<NS::Game::Player::WalkPlayerState>())
        {
            return "Walk";
        }
        if (player.States().IsCurrent<NS::Game::Player::BrakePlayerState>())
        {
            return "Brake";
        }
        if (player.States().IsCurrent<NS::Game::Player::FallPlayerState>())
        {
            return "Fall";
        }
        if (player.States().IsCurrent<NS::Game::Player::BodySlamPlayerState>())
        {
            return "BodySlam";
        }
        if (player.States().IsCurrent<NS::Game::Player::ReboundPlayerState>())
        {
            return "Rebound";
        }
        return "Other";
    }

    void ExpectSameVector(const NS::Core::Vector3& actual, const NS::Core::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 0.00001f);
        EXPECT_NEAR(actual.y, expected.y, 0.00001f);
        EXPECT_NEAR(actual.z, expected.z, 0.00001f);
    }

    // 手前の置物へ溜めた突進を出し、止めの頭まで回す。届いた場合 true
    // waitForFreeze が偽なら、止めの頭を待つ検知のフレームで止める
    // 突進は 1 フレームの入口から出す。BodySlam を直に呼ぶと先行入力が残り、止めが明けた後にもう 1 度出る
    bool SlamIntoTheRock(Player& player, NS::Game::Level::MapObj& rock, bool waitForFreeze)
    {
        player.RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 10; ++frame)
        {
            player.Update(false);
            rock.Update();
            if (waitForFreeze && player.Resolver().FreezeBeganThisStep())
            {
                return true;
            }
            if (!waitForFreeze && player.Resolver().LastImpact().sequence > 0)
            {
                return true;
            }
        }
        return false;
    }
} // namespace

TEST(PlayerUpdatePipeline, ImpactObservationDoesNotJudgeOrMoveThePlayer)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetVelocity(NS::Core::Vector3{2.0f, 4.0f, 3.0f});
    const NS::Core::Vector3 position = player->Root().Position();
    const NS::Core::Vector3 velocity = player->Body().Velocity();
    player->Resolver().ObserveImpact();
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
    ExpectSameVector(player->Root().Position(), position);
    ExpectSameVector(player->Body().Velocity(), velocity);
}

// 左右の寄せは消した。脇の相手へ向きを曲げると、放った後に矢印とずれて当て所が見えない所で動く
TEST(PlayerUpdatePipeline, SlamHeadingStaysOnTheAimBesideAnOffAxisTarget)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.5f, 3.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    const NS::Core::Vector3 before = player->BodySlamVelocity();
    for (int frame = 0; frame < 2; ++frame)
    {
        SCOPED_TRACE(frame);
        player->Update(false);
        ASSERT_TRUE(player->IsBodySlamming());
        EXPECT_NEAR(player->Body().Velocity().x, before.x, 0.00001f);
        EXPECT_NEAR(player->Body().Velocity().z, before.z, 0.00001f);
    }
}

// 溜めた突進の水平の書き手は突進の状態 1 つ。壁に押し付けられて水平が 0
// に潰れても、次のフレームに発動時の向きと速さへ戻る
TEST(PlayerUpdatePipeline, SquashedSlamRegainsItsHeadingFromTheState)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 40.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Body().SetLateralVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    player->Update(false);
    ASSERT_TRUE(player->IsBodySlamming());
    const NS::Core::Vector3 expected = player->BodySlamVelocity();
    EXPECT_GT(expected.z, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().x, expected.x, 0.00001f);
    EXPECT_NEAR(player->Body().Velocity().z, expected.z, 0.00001f);
}

TEST(PlayerUpdatePipeline, OneObservationCannotBeginFreezeTwice)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Resolver().ObserveImpact();
    player->Resolver().StepState();
    ASSERT_EQ(player->Resolver().LastImpact().sequence, 1u);
    ASSERT_FALSE(player->Resolver().FreezeBeganThisStep());
    player->Resolver().StepState();
    EXPECT_FALSE(player->Resolver().FreezeBeganThisStep());
    EXPECT_FALSE(player->Resolver().IsHitStopping());
    player->Resolver().ObserveImpact();
    player->Resolver().StepState();
    EXPECT_TRUE(player->Resolver().FreezeBeganThisStep());
    EXPECT_TRUE(player->Resolver().IsHitStopping());
}

// 1 フレームを進める入口は Player::Update だけ。裁定の部品を単独で回しても、観測も裁定も走らない
TEST(PlayerUpdatePipeline, TickingTheResolverPartAloneDoesNotJudge)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Resolver().OnUpdate();
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FALSE(player->Resolver().IsHitStopping());
}

TEST(PlayerUpdatePipeline, RemovingTheObservedTargetCannotApplyAStaleImpact)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Resolver().ObserveImpact();
    scene.Objects().RemoveByObjectId(2);
    player->Resolver().StepState();
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FALSE(player->Resolver().IsHitStopping());
}

// 止めの持ち主は裁定役の数え。身体の部品は外さず、止めの間は位置も状態の歩も進まない
TEST(PlayerUpdatePipeline, HitStopHoldsTheBodyWithoutSwitchingItOff)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    EXPECT_TRUE(player->Body().IsActive());
    EXPECT_TRUE(player->Resolver().IsHitStopping());
    EXPECT_FALSE(player->CanMoveBody());
    const NS::Core::Vector3 position = player->Root().Position();
    const std::uint32_t stateStep = player->States().StepsInState();
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
        rock->Update();
        if (!player->Resolver().IsHitStopping())
        {
            break;
        }
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Position(), position);
        EXPECT_EQ(player->States().StepsInState(), stateStep);
    }
    EXPECT_FALSE(player->Resolver().IsHitStopping());
    EXPECT_TRUE(player->CanMoveBody());
}

// 身体の部品を外したのは試しで、止めの明けはそれを上書きしない
TEST(PlayerUpdatePipeline, ReleasingTheHitStopLeavesASwitchedOffBodyAlone)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    player->Body().SetActive(false);
    bool released = false;
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
        rock->Update();
        if (player->Resolver().ReleasedThisStep())
        {
            released = true;
        }
    }
    EXPECT_TRUE(released);
    EXPECT_FALSE(player->Body().IsActiveSelf());
}

// やり直しは走っている当たりのタイムラインを捨てる。出現位置で弾かれず、元の位置へ戻った置物へ明けも止めの頭も届かない
TEST(PlayerUpdatePipeline, RestartDropsTheHitStopAndItsReservation)
{
    for (const bool waitForFreeze : {true, false})
    {
        SCOPED_TRACE(waitForFreeze);
        NS::Obj::Scene scene;
        Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
        ASSERT_NE(player, nullptr);
        (void)scene.BeginPlayBaseline();
        NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        const NS::Core::Vector3 rockHome = rock->Root().Position();
        ASSERT_TRUE(SlamIntoTheRock(*player, *rock, waitForFreeze));
        NS::Game::Level::CourseDirector* director =
            NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(scene);
        ASSERT_NE(director, nullptr);
        director->RestartCourse();
        for (int frame = 0; frame < 20; ++frame)
        {
            player->Update(false);
            rock->Update();
            SCOPED_TRACE(frame);
            EXPECT_FALSE(player->IsRebounding());
            EXPECT_FALSE(player->Resolver().FreezeBeganThisStep());
            EXPECT_FALSE(player->Resolver().ReleasedThisStep());
            EXPECT_FALSE(player->Resolver().IsHitStopping());
            ExpectSameVector(rock->Root().Position(), rockHome);
            ExpectSameVector(player->ModelPart()->DrawScale(), NS::Core::Vector3{1.0f, 1.0f, 1.0f});
        }
    }
}

// 止めの途中でプレイを終えると、潰れた形のまま残らず止めも消える
TEST(PlayerUpdatePipeline, EndingPlayDuringHitStopRestoresTheShape)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    ASSERT_TRUE(player->Resolver().IsShapeAnimating());
    ASSERT_LT(player->ModelPart()->DrawScale().z, 1.0f);
    player->OnEndPlay();
    EXPECT_TRUE(player->ModelPart()->DrawScale() == (NS::Core::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(player->Resolver().IsHitStopping());
    EXPECT_FALSE(player->Resolver().IsShapeAnimating());
}

// 溜めの構えは描く形の倍率に出る
TEST(PlayerAppearance, ComposesTheStanceIntoTheDrawScale)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    for (int frame = 0; frame < 120 && !player->ChargeJudge().IsCharging(); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player->ModelPart()->DrawScale().y, 0.95f);
}

// 当てた瞬間の潰れと明けの伸びは描く形の倍率に出て、根のスケールは 1 のまま。戻しの最後のフレームでちょうど 1
TEST(CollisionImpact, HitStopSquashIsDrawnAndTheRootStaysOne)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    ASSERT_FALSE(player->Resolver().LastImpact().broke);
    // 欄「潰れの厚み」0.7 を進む向きの成分の 2 乗で混ぜ、縦は欄「潰れの伸び上がり」1.1
    const NS::Core::Vector3 dir = player->Resolver().LastImpact().impactDir;
    const NS::Core::Vector3 squash{1.0f - 0.3f * dir.x * dir.x, 1.1f, 1.0f - 0.3f * dir.z * dir.z};
    const NS::Core::Vector3 one{1.0f, 1.0f, 1.0f};
    bool released = false;
    for (int frame = 0; frame < 30 && !released; ++frame)
    {
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Scale(), one);
        ExpectSameVector(player->ModelPart()->DrawScale(), squash);
        player->Update(false);
        rock->Update();
        released = player->Resolver().ReleasedThisStep();
    }
    ASSERT_TRUE(released);
    // 反発の明けは縦へ欄「弾け伸びの倍率」1.2
    ExpectSameVector(player->ModelPart()->DrawScale(), NS::Core::Vector3{1.0f, 1.2f, 1.0f});
    for (int frame = 0; frame < 30 && player->Resolver().IsShapeAnimating(); ++frame)
    {
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Scale(), one);
        player->Update(false);
        rock->Update();
    }
    ASSERT_FALSE(player->Resolver().IsShapeAnimating());
    EXPECT_TRUE(player->ModelPart()->DrawScale() == one);
    EXPECT_TRUE(player->Root().Scale() == one);
}

// 接地して当てた明けの伸びは描く形の下端の真ん中を中心に掛かり、玉の下端が床の下へ出ない
TEST(PlayerAppearance, ReleaseStretchKeepsTheDrawnBottomOnTheFloor)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 2.5f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    for (int frame = 0; frame < 60; ++frame)
    {
        player->Update(false);
        rock->Update();
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    bool released = false;
    for (int frame = 0; frame < 30 && !released; ++frame)
    {
        player->Update(false);
        rock->Update();
        released = player->Resolver().ReleasedThisStep();
    }
    ASSERT_TRUE(released);
    ASSERT_FALSE(player->Resolver().LastImpact().broke);
    ASSERT_TRUE(player->Resolver().IsShapeAnimating());
    NS::Obj::Model* model = player->ModelPart();
    // 試しには mesh が無いので、玉の局所の境界を差し、回転を外して形の伸びだけを測る
    const float radius = player->Collider().CapsuleRadius();
    NS::Core::AABB local{};
    local.Center = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    local.Extents = NS::Core::Vector3{radius, radius, radius};
    model->SetLocalBoundsOverride(local);
    model->SnapLocalRotation(NS::Core::Quaternion::Identity);
    NS::Core::AABB drawn{};
    local.Transform(drawn, model->DrawWorldMatrix(1.0f));
    const float bottom = drawn.Center.y - drawn.Extents.y;
    EXPECT_NEAR(bottom, player->Root().Position().y - radius, 0.001f);
    EXPECT_GE(bottom, -0.001f);
}

// Inspector で裁定役を外すと、走っている当たりのタイムラインを捨てる。入れ直しても遅れて弾かれない
TEST(PlayerUpdatePipeline, SwitchingTheResolverOffDropsItsHitStop)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    player->Resolver().SetEnabled(false);
    player->Update(false);
    rock->Update();
    EXPECT_TRUE(player->CanMoveBody());
    player->Resolver().SetEnabled(true);
    for (int frame = 0; frame < 20; ++frame)
    {
        player->Update(false);
        rock->Update();
        SCOPED_TRACE(frame);
        EXPECT_FALSE(player->Resolver().ReleasedThisStep());
        EXPECT_FALSE(player->IsRebounding());
    }
}

TEST(PlayerUpdatePipeline, PausedMovementKeepsItsStoredVelocityDuringSlamControl)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.5f, 3.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Body().SetVelocity(NS::Core::Vector3{0.0f, 2.0f, 0.0f});
    player->Body().SetActive(false);
    const NS::Core::Vector3 position = player->Root().Position();
    const NS::Core::Vector3 velocity = player->Body().Velocity();
    const std::uint32_t stateStep = player->States().StepsInState();
    player->Update(false);
    ExpectSameVector(player->Root().Position(), position);
    ExpectSameVector(player->Body().Velocity(), velocity);
    EXPECT_EQ(player->States().StepsInState(), stateStep);
}

// 編集の休止の持ち主は世界の駆動だけ。身体と入力の部品を起こしたままでも、止めた世界では自機が動かない
TEST(PlayerUpdatePipeline, StoppedWorldHoldsThePlayerWithItsPartsAwake)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(player->Body().IsActive());
    ASSERT_TRUE(player->Input().IsActive());
    scene.SetSimulationEnabled(false);
    player->Input().SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    // 入力の段が実機の入力で歩きを消しても、回っていればこの速さで動く
    player->Body().SetVelocity(NS::Core::Vector3{0.0f, 2.0f, 0.0f});
    const NS::Core::Vector3 position = player->Root().Position();

    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }

    ExpectSameVector(player->Root().Position(), position);
}

// 溜めて当てる組とタップの組を、本番の 1 フレームの入口で回した数字の基準
// 末尾の列は、状態・描く形の倍率・身体が動いているかが変わったフレームだけを並べた物。間のフレームは直前の行と同じ
TEST(PlayerUpdatePipeline, StateTransitionPreservesChargeAndTapTrajectories)
{
    const nlohmann::json baseline = nlohmann::json::parse(
        R"([
        [0, 94, 95, 107, [
            [0, 0, 0.649999976, 0, 0, 0, 0, 0.04, 0.5, 2.5],
            [90, 0, 0.6500000358, 0.3333333433, 0, 0, 20, 0.04, 0.5, 2.5],
            [92, 0, 0.6499999762, 1.0, 0, 0, 20, 0.14, 0.5, 2.5],
            [93, 0, 0.6499999166, 1.3333333731, 0, 0, 20, 0.14, 0.5, 2.5],
            [100, -0.0123793595, 0.6500000954, 1.3523943424, -0.8675079346, 1.67509e-7, 0.104101181, 0.14, 0.5, 2.5454165936],
            [110, -0.02943230793, 1.116387725, 1.210286379, -0.2557942271, 6.683314323, -2.131618738, 0.14, 1.384893417, 5.983239174],
            [140, -0.1573294252, 2.848669291, 0.1444766968, -0.2557942271, 0.641646266, -2.131618738, 0.14, 4.491162777, 32.10754395],
            [189, -0.294336319, 1.149999738, -0.9972489476, 0, 0, 0, 0.14, 0.501000941, 74.210495]
        ], [
            [0, "Idle", 1, 0.9700000286, 1, true],
            [11, "Idle", 1, 0.9499999881, 1, true],
            [90, "BodySlam", 1, 1, 1, true],
            [94, "Walk", 1, 1, 1, true],
            [95, "Walk", 1, 1.100000024, 0.6999999881, false],
            [107, "Rebound", 1, 1.200000048, 1, true],
            [108, "Rebound", 1, 1.100000024, 1, true],
            [109, "Rebound", 1, 1, 1, true],
            [110, "Rebound", 1, 0.8999999762, 1, true],
            [111, "Rebound", 1, 0.9333333373, 1, true],
            [112, "Rebound", 1, 0.9666666389, 1, true],
            [113, "Rebound", 1, 1, 1, true],
            [170, "Rebound", 1.118034005, 0.8000000119, 1.118034005, true],
            [171, "Idle", 1.095445156, 0.8333333731, 1.095445156, true],
            [172, "Walk", 1.074172258, 0.8666666746, 1.074172258, true],
            [173, "Walk", 1.054092646, 0.8999999762, 1.054092646, true],
            [174, "Walk", 1.035098314, 0.9333333373, 1.035098314, true],
            [175, "Idle", 1.017095208, 0.9666666985, 1.017095208, true],
            [176, "Idle", 1, 1, 1, true]
        ]],
        [1, 14, 15, 18, [
            [0, 0, 0.649999976, 0, 0, 0, 0, 0, 0.5, 3],
            [3, 0, 0.6973332763, 0.1666666716, 0, 2.839999914, 10, 0, 0.5, 3],
            [4, 0, 0.7419999242, 0.3333333433, 0, 2.679999828, 10, 0, 0.5, 3],
            [10, 0, 0.953999877, 1.333333254, 0, 1.719999552, 10, 0, 0.5, 3],
            [30, 0, 1.669279456, 1.836697578, 0, 1.664880991, -0.5998571515, 0, 1.712962747, 9.446973801],
            [60, 0, 1.149999976, 1.53676939, 0, 0, -0.5998571515, 0, 0.5009999871, 24.31210518],
            [89, 0, 1.149999738, 1.53676939, 0, 0, 0, 0, 0.500999987, 36.63962555]
        ], [
            [0, "Idle", 1, 0.9700000286, 1, true],
            [3, "BodySlam", 1, 1, 1, true],
            [14, "Fall", 1, 1, 1, true],
            [15, "Fall", 1, 1.100000024, 0.6999999881, false],
            [18, "Rebound", 1, 1.200000048, 1, true],
            [19, "Rebound", 1, 1.100000024, 1, true],
            [20, "Rebound", 1, 1, 1, true],
            [21, "Rebound", 1, 0.8999999762, 1, true],
            [22, "Rebound", 1, 0.9333333373, 1, true],
            [23, "Rebound", 1, 0.9666666389, 1, true],
            [24, "Rebound", 1, 1, 1, true],
            [59, "Rebound", 1.118034005, 0.8000000119, 1.118034005, true],
            [60, "Idle", 1.095445156, 0.8333333731, 1.095445156, true],
            [61, "Idle", 1.074172258, 0.8666666746, 1.074172258, true],
            [62, "Idle", 1.054092646, 0.8999999762, 1.054092646, true],
            [63, "Idle", 1.035098314, 0.9333333373, 1.035098314, true],
            [64, "Idle", 1.017095208, 0.9666666985, 1.017095208, true],
            [65, "Idle", 1, 1, 1, true]
        ]]
    ])");
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        const bool charge = scenario == 0;
        // 止めの長さは移す前の式が出した長さ (溜め 12・タップ 3) を、移す前の返りを写したタイムラインに置く
        const ScopedHitTimelineDirectory timelines("Trajectories");
        int legacyStopSteps = 3;
        if (charge)
        {
            legacyStopSteps = 12;
        }
        ScopedHitTimelineDirectory::SetBothTiers(MakeLegacyHitTimeline(legacyStopSteps));
        NS::Obj::Scene scene;
        float targetX = 0.0f;
        float targetZ = 3.0f;
        int frames = 90;
        int holdFrames = 3;
        if (charge)
        {
            targetX = 0.04f;
            targetZ = 2.5f;
            frames = 190;
            holdFrames = 90;
        }
        Player* player = PlacePipelinePlayer(scene, targetX, targetZ);
        ASSERT_NE(player, nullptr);
        NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        int impact = -1;
        int freeze = -1;
        int release = -1;
        const nlohmann::json& expected = baseline[scenario];
        for (int frame = 0; frame < frames; ++frame)
        {
            const bool held = frame < holdFrames;
            if (held)
            {
                player->Body().SetGrounded(true);
            }
            if (charge && frame == 92)
            {
                rock->Root().ShiftPosition(NS::Core::Vector3{0.1f, 0.0f, 0.0f});
            }
            player->Update(held);
            if (impact < 0 && player->Resolver().LastImpact().sequence > 0)
            {
                impact = frame;
            }
            if (freeze < 0 && player->Resolver().FreezeBeganThisStep())
            {
                freeze = frame;
            }
            if (release < 0 && player->Resolver().ReleasedThisStep())
            {
                release = frame;
            }
            rock->Update();
            const nlohmann::json* pose = nullptr;
            for (const nlohmann::json& change : expected[5])
            {
                if (change[0].get<int>() <= frame)
                {
                    pose = &change;
                }
            }
            ASSERT_NE(pose, nullptr);
            {
                SCOPED_TRACE(scenario);
                SCOPED_TRACE(frame);
                EXPECT_EQ(StateName(*player), (*pose)[1].get<std::string>());
                ExpectSameVector(
                    player->ModelPart()->DrawScale(),
                    NS::Core::Vector3{(*pose)[2].get<float>(), (*pose)[3].get<float>(), (*pose)[4].get<float>()});
                // 構え・潰れ・伸び・着地の潰れのどれの間も、根のスケールは配置の値のまま
                EXPECT_TRUE(player->Root().Scale() == (NS::Core::Vector3{1.0f, 1.0f, 1.0f}));
                EXPECT_EQ(player->CanMoveBody(), (*pose)[5].get<bool>());
            }
            for (const nlohmann::json& sample : expected[4])
            {
                if (sample[0].get<int>() != frame)
                {
                    continue;
                }
                SCOPED_TRACE(scenario);
                SCOPED_TRACE(frame);
                ExpectSameVector(
                    player->Root().Position(),
                    NS::Core::Vector3{sample[1].get<float>(), sample[2].get<float>(), sample[3].get<float>()});
                ExpectSameVector(
                    player->Body().Velocity(),
                    NS::Core::Vector3{sample[4].get<float>(), sample[5].get<float>(), sample[6].get<float>()});
                ExpectSameVector(
                    rock->Root().Position(),
                    NS::Core::Vector3{sample[7].get<float>(), sample[8].get<float>(), sample[9].get<float>()});
            }
        }
        EXPECT_EQ(impact, expected[1].get<int>());
        EXPECT_EQ(freeze, expected[2].get<int>());
        EXPECT_EQ(release, expected[3].get<int>());
    }
}
