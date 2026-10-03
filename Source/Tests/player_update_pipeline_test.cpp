#include "Game/Level/CollisionInput.h"
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
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"

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
        scene.MainCamera()->SetPosition(NS::Core::Vector3{});
        scene.MainCamera()->SetTarget(NS::Core::Vector3{0.0f, 0.0f, 1.0f});
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
    // waitForFreeze が偽なら、止めの予約だけが残る検知のフレームで止める
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

TEST(PlayerUpdatePipeline, ObservationDoesNotAdvanceChargeOrMoveThePlayer)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetVelocity(NS::Core::Vector3{2.0f, 4.0f, 3.0f});
    const NS::Core::Vector3 position = player->Root().Position();
    const NS::Core::Vector3 velocity = player->Body().Velocity();
    player->ChargeControl().Observe(true);
    player->Resolver().ObserveImpact();
    EXPECT_FALSE(player->ChargeControl().Judge().IsHeld());
    EXPECT_FALSE(player->IsCurled());
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
    ExpectSameVector(player->Root().Position(), position);
    ExpectSameVector(player->Body().Velocity(), velocity);
    player->ChargeControl().AdvanceState(NS::Platform::FrameTimer::FixedDelta());
    EXPECT_TRUE(player->ChargeControl().Judge().JustPressed());
    EXPECT_TRUE(player->IsCurled());
}

TEST(PlayerUpdatePipeline, OneObservationCannotAdvanceChargeTwice)
{
    Player player;
    player.ChargeControl().OnStart();
    player.ChargeControl().Observe(true);
    player.ChargeControl().AdvanceState(0.1f);
    player.ChargeControl().AdvanceState(0.1f);
    EXPECT_TRUE(player.ChargeControl().Judge().JustPressed());
    EXPECT_FALSE(player.ChargeControl().IsCharging());
    player.ChargeControl().Observe(true);
    player.ChargeControl().AdvanceState(0.1f);
    EXPECT_TRUE(player.ChargeControl().Judge().JustStartedCharging());
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

// やり直しは止めと止めの予約を捨てる。出現位置で弾かれず、元の位置へ戻った置物へ明けも止めの頭も届かない
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
            ExpectSameVector(player->Root().Scale(), NS::Core::Vector3{1.0f, 1.0f, 1.0f});
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
    ASSERT_TRUE(player->Resolver().IsScaleAnimating());
    ASSERT_LT(player->Root().Scale().z, 1.0f);
    player->Resolver().OnEndPlay();
    ExpectSameVector(player->Root().Scale(), NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    EXPECT_FALSE(player->Resolver().IsHitStopping());
    EXPECT_FALSE(player->Resolver().IsScaleAnimating());
}

// Inspector で裁定役を外すと、持っていた止めと予約を捨てる。入れ直しても遅れて弾かれない
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

// 溜めて当てる組とタップの組を、本番の 1 フレームの入口で回した数字の基準
// 末尾の列は、状態・根のスケール・身体が動いているかが変わったフレームだけを並べた物。間のフレームは直前の行と同じ
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
            [171, "Idle", 1, 1, 1, true],
            [172, "Walk", 1, 1, 1, true],
            [175, "Idle", 1, 1, 1, true]
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
            [60, "Idle", 1, 1, 1, true]
        ]]
    ])");
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        NS::Obj::Scene scene;
        const bool charge = scenario == 0;
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
                    player->Root().Scale(),
                    NS::Core::Vector3{(*pose)[2].get<float>(), (*pose)[3].get<float>(), (*pose)[4].get<float>()});
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
