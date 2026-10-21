#include "Game/Level/CourseDirector.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Level/PropActor.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BeforeContactPlayerState.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/TackleHitPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace
{
    // 裁定の観測と決定を、自機の段と同じ引数で単独に呼ぶ。答えにある自機を動かす事はしない
    void ObserveImpact(PlayerActor& player)
    {
        player.Resolver().ObserveImpact(player.SlamMotion(), &player.Collider());
    }

    void StepImpact(PlayerActor& player)
    {
        player.Resolver().StepState(player.SlamMotion(),
                                    &player.Params(),
                                    &player.Body(),
                                    &player.Collider(),
                                    player.ModelClassObj(),
                                    player.HitReactionClassObj());
    }

    PlayerActor* PlacePipelinePlayer(NS::Scene& scene, float targetX = 0.0f, float targetZ = 3.0f)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(player, "PlayerActor");
        NS::SetObjectJsonId(player, 1);
        NS::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, "PropActor");
        NS::SetObjectJsonId(rock, 2);
        NS::SetObjectPosition(rock, NS::Vector3{targetX, 0.5f, targetZ});
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Objects().FindByObjectId(1));
    }

    // 見本の列に書く状態の綴り。表に無い状態は Other
    std::string_view StateName(const PlayerActor& player)
    {
        if (player.States().IsCurrent<GL::Player::IdlePlayerState>())
        {
            return "Idle";
        }
        if (player.States().IsCurrent<GL::Player::WalkPlayerState>())
        {
            return "Walk";
        }
        if (player.States().IsCurrent<GL::Player::BrakePlayerState>())
        {
            return "Brake";
        }
        if (player.States().IsCurrent<GL::Player::FallPlayerState>())
        {
            return "Fall";
        }
        if (player.States().IsCurrent<GL::Player::BodySlamPlayerState>())
        {
            return "BodySlam";
        }
        if (player.States().IsCurrent<GL::Player::BeforeContactPlayerState>())
        {
            return "BeforeContact";
        }
        if (player.States().IsCurrent<GL::Player::TackleHitPlayerState>())
        {
            return "TackleHit";
        }
        if (player.States().IsCurrent<GL::Player::ReboundPlayerState>())
        {
            return "Rebound";
        }
        return "Other";
    }

    void ExpectSameVector(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 0.00001f);
        EXPECT_NEAR(actual.y, expected.y, 0.00001f);
        EXPECT_NEAR(actual.z, expected.z, 0.00001f);
    }

    // 手前の置物へ溜めた突進を出し、当たりの状態に入って止めが始まった歩まで回す。届いた場合 true
    // waitForFreeze が偽なら、当たりの状態に入る前の検知のフレームで止める
    // 突進は 1 フレームの入口から出す。BodySlam を直に呼ぶと先行入力が残り、止めが明けた後にもう 1 度出る
    bool SlamIntoTheRock(PlayerActor& player, GL::Level::PropActor& rock, bool waitForFreeze)
    {
        player.RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 10; ++frame)
        {
            player.Update(false);
            rock.Update();
            if (waitForFreeze && player.IsInTackleHit())
            {
                return true;
            }
            if (!waitForFreeze && player.Resolver().Answer().lastImpact.sequence > 0)
            {
                return true;
            }
        }
        return false;
    }

    // 1 歩回し、当たりの状態に居た自機がその歩で出た (止めが明けた) 場合 true
    bool StepAndSeeTheRelease(PlayerActor& player, GL::Level::PropActor& rock)
    {
        const bool wasInHit = player.IsInTackleHit();
        player.Update(false);
        rock.Update();
        return wasInHit && !player.IsInTackleHit();
    }

    // 移す前の式が出した止めの長さ stopSteps の当たりを写した段の束。形は止めの間潰れ、明けに伸びて戻る
    NS::HitReactionSet MakeLegacyHitSet(std::string name, int stopSteps)
    {
        const float n = static_cast<float>(stopSteps);
        NS::HitShapeItem shape;
        shape.along.count = 3;
        shape.along.keys[0] = NS::Curve::Key{0.0f, 0.7f};
        shape.along.keys[1] = NS::Curve::Key{n - 1.0f, 0.7f};
        shape.along.keys[2] = NS::Curve::Key{n, 1.0f};
        shape.height.count = 5;
        shape.height.keys[0] = NS::Curve::Key{0.0f, 1.1f};
        shape.height.keys[1] = NS::Curve::Key{n - 1.0f, 1.1f};
        shape.height.keys[2] = NS::Curve::Key{n, 1.2f};
        shape.height.keys[3] = NS::Curve::Key{n + 3.0f, 0.9f};
        shape.height.keys[4] = NS::Curve::Key{n + 6.0f, 1.0f};
        NS::HitReactionSet set;
        set.name = std::move(name);
        set.stopFrames = stopSteps;
        set.entries.push_back(NS::HitReactionEntry{.item = shape, .length = stopSteps + 6});
        return set;
    }

    // 自機の .actor の段の束 2 つを MakeLegacyHitSet に替え、当たる直前の動作を外す (予測の線を引かない)
    void UseLegacyHitSets(nlohmann::json& actorFile, int stopSteps)
    {
        std::vector<NS::HitReactionSet> sets = NS::ReadHitReactionSets("PlayerActor", actorFile);
        for (const GL::Level::HitTier tier : {GL::Level::HitTier::Center, GL::Level::HitTier::Wide})
        {
            const std::string name = GL::Level::TackleSetName("体当たり", tier);
            bool replaced = false;
            for (NS::HitReactionSet& set : sets)
            {
                if (set.name == name)
                {
                    set = MakeLegacyHitSet(name, stopSteps);
                    replaced = true;
                }
            }
            EXPECT_TRUE(replaced) << name;
        }
        NS::WriteHitReactionSets(actorFile, sets);
        NS::WriteActorActions(actorFile, {});
    }
} // namespace

TEST(PlayerUpdatePipeline, ImpactObservationDoesNotJudgeOrMoveThePlayer)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetVelocity(NS::Vector3{2.0f, 4.0f, 3.0f});
    const NS::Vector3 position = player->Root().Position();
    const NS::Vector3 velocity = player->Body().Velocity();
    ObserveImpact(*player);
    EXPECT_EQ(player->Resolver().Answer().lastImpact.sequence, 0u);
    ExpectSameVector(player->Root().Position(), position);
    ExpectSameVector(player->Body().Velocity(), velocity);
}

// 左右の寄せは消した。脇の相手へ向きを曲げると、放った後に矢印とずれて当て所が見えない所で動く
TEST(PlayerUpdatePipeline, SlamHeadingStaysOnTheAimBesideAnOffAxisTarget)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.5f, 3.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    const NS::Vector3 before = player->BodySlamVelocity();
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
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 40.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Body().SetLateralVelocity(NS::Vector3{0.0f, 0.0f, 0.0f});
    player->Update(false);
    ASSERT_TRUE(player->IsBodySlamming());
    const NS::Vector3 expected = player->BodySlamVelocity();
    EXPECT_GT(expected.z, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().x, expected.x, 0.00001f);
    EXPECT_NEAR(player->Body().Velocity().z, expected.z, 0.00001f);
}

// 当たりの状態へ移る頼みは、検知の次の観測と裁定の組で出る。観測の無い裁定は進まないので出ない
TEST(PlayerUpdatePipeline, OneObservationCannotBeginTheHitTwice)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    ObserveImpact(*player);
    StepImpact(*player);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.sequence, 1u);
    ASSERT_FALSE(player->Resolver().Answer().beginHit);
    StepImpact(*player);
    EXPECT_FALSE(player->Resolver().Answer().beginHit);
    EXPECT_TRUE(player->Resolver().IsHitAwaitingEntry());
    EXPECT_EQ(player->Resolver().Answer().lastImpact.sequence, 1u);
    ObserveImpact(*player);
    StepImpact(*player);
    EXPECT_TRUE(player->Resolver().Answer().beginHit);
}

TEST(PlayerUpdatePipeline, RemovingTheObservedTargetCannotApplyAStaleImpact)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    ObserveImpact(*player);
    scene.Objects().RemoveByObjectId(2);
    StepImpact(*player);
    EXPECT_EQ(player->Resolver().Answer().lastImpact.sequence, 0u);
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FALSE(player->Resolver().IsHitAwaitingEntry());
}

// 止めの持ち主は基底の止めの残りと当たりの状態。身体の部品は外さず、止めの間は位置も状態の歩も進まない
TEST(PlayerUpdatePipeline, HitStopHoldsTheBodyWithoutSwitchingItOff)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    EXPECT_TRUE(player->IsHitStopping());
    EXPECT_FALSE(player->CanMoveBody());
    const NS::Vector3 position = player->Root().Position();
    const NS::uint32 stateStep = player->States().StepsInState();
    // 止めが明けて反動へ移る歩までは止まったまま
    bool released = false;
    for (int frame = 0; frame < 30 && !released; ++frame)
    {
        released = StepAndSeeTheRelease(*player, *rock);
        if (released)
        {
            break;
        }
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Position(), position);
        EXPECT_EQ(player->States().StepsInState(), stateStep);
    }
    EXPECT_TRUE(released);
    EXPECT_FALSE(player->IsHitStopping());
    EXPECT_TRUE(player->CanMoveBody());
}

// やり直しは当たりの状態と止めと当たりの状態へ移る待ちを捨てる。出現位置で弾かれず、元の位置へ戻った置物へ明けも当たったも届かない
TEST(PlayerUpdatePipeline, RestartDropsTheHitStopAndItsReservation)
{
    for (const bool waitForFreeze : {true, false})
    {
        SCOPED_TRACE(waitForFreeze);
        NS::Scene scene;
        PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
        ASSERT_NE(player, nullptr);
        GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene);
        ASSERT_NE(director, nullptr);
        director->StartCourse();
        GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        const NS::Vector3 rockHome = rock->Root().Position();
        ASSERT_TRUE(SlamIntoTheRock(*player, *rock, waitForFreeze));
        director->RestartCourse();
        for (int frame = 0; frame < 20; ++frame)
        {
            player->Update(false);
            rock->Update();
            SCOPED_TRACE(frame);
            EXPECT_FALSE(player->IsRebounding());
            EXPECT_FALSE(player->IsInTackleHit());
            EXPECT_FALSE(player->IsHitStopping());
            EXPECT_FALSE(rock->IsFrozen());
            ExpectSameVector(rock->Root().Position(), rockHome);
            ExpectSameVector(player->ModelClassObj()->GetDrawScale(), NS::Vector3{1.0f, 1.0f, 1.0f});
        }
    }
}

// 止めの途中でプレイを終えると、潰れた形のまま残らず止めも消える
TEST(PlayerUpdatePipeline, EndingPlayDuringHitStopRestoresTheShape)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    ASSERT_TRUE(player->ModelClassObj()->IsDrawingShape());
    ASSERT_LT(player->ModelClassObj()->GetDrawScale().z, 1.0f);
    player->OnEndPlay();
    EXPECT_TRUE(player->ModelClassObj()->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(player->IsHitStopping());
    EXPECT_FALSE(player->IsInTackleHit());
    EXPECT_FALSE(player->ModelClassObj()->IsDrawingShape());
}

// 溜めの構えは描く形の倍率に出る
TEST(PlayerAppearance, ComposesTheStanceIntoTheDrawScale)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    for (int frame = 0; frame < 120 && !player->ChargeJudge().IsCharging(); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player->ModelClassObj()->GetDrawScale().y, 0.95f);
}

// 当てた瞬間の潰れと明けの伸びは描く形の倍率に出て、根のスケールは 1 のまま。戻しの最後のフレームでちょうど 1
// 止めの途中で、衝撃が背中に着いたフレームに横と高さを 1.3 倍へ一度膨らませ、5 フレームで潰れの形へ戻す
TEST(CollisionImpact, HitStopSquashIsDrawnAndTheRootStaysOne)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(*player, *rock, true));
    ASSERT_FALSE(player->Resolver().Answer().lastImpact.broke);
    // 欄「潰れの厚み」0.7 を進む向きの成分の 2 乗で混ぜ、縦は欄「潰れの伸び上がり」1.1
    const NS::Vector3 dir = player->Resolver().Answer().lastImpact.impactDir;
    const NS::Vector3 squash{1.0f - 0.3f * dir.x * dir.x, 1.1f, 1.0f - 0.3f * dir.z * dir.z};
    const NS::Vector3 one{1.0f, 1.0f, 1.0f};
    bool released = false;
    int bulgeAt = -1;
    for (int frame = 0; frame < 30 && !released; ++frame)
    {
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Scale(), one);
        // 膨らみは高さの倍率が 1.1 を超えたフレームから。横は進む向きに直角な軸 (成分の 2 乗を入れ替えて混ぜる)
        if (bulgeAt < 0 && player->ModelClassObj()->GetDrawScale().y > 1.1f + 1.0e-4f)
        {
            bulgeAt = frame;
        }
        float left = 0.0f;
        if (bulgeAt >= 0)
        {
            left = std::max(1.0f - static_cast<float>(frame - bulgeAt) / 5.0f, 0.0f);
        }
        const float side = 0.3f * left;
        ExpectSameVector(
            player->ModelClassObj()->GetDrawScale(),
            NS::Vector3{squash.x + side * dir.z * dir.z, squash.y + 0.2f * left, squash.z + side * dir.x * dir.x});
        released = StepAndSeeTheRelease(*player, *rock);
    }
    ASSERT_TRUE(released);
    // 膨らみは止めの途中 (止めの 12 フレームの 7 フレーム目) に来る
    EXPECT_GT(bulgeAt, 0);
    // 明けは潰れ 0.7 から 3 フレームで突進の向きへ 1.25 まで伸び、縦は 1.1 から 0.9 へ細る。明けはその 1 フレーム目
    const float along = 0.7f + 0.55f / 3.0f;
    const float height = 1.1f - 0.2f / 3.0f;
    ExpectSameVector(player->ModelClassObj()->GetDrawScale(),
                     NS::Vector3{1.0f - (1.0f - along) * dir.x * dir.x, height, 1.0f - (1.0f - along) * dir.z * dir.z});
    for (int frame = 0; frame < 30 && player->ModelClassObj()->IsDrawingShape(); ++frame)
    {
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Scale(), one);
        player->Update(false);
        rock->Update();
    }
    ASSERT_FALSE(player->ModelClassObj()->IsDrawingShape());
    EXPECT_TRUE(player->ModelClassObj()->GetDrawScale() == one);
    EXPECT_TRUE(player->Root().Scale() == one);
}

// 接地して当てた明けの伸びは描く形の下端の真ん中を中心に掛かり、玉の下端が床の下へ出ない
TEST(PlayerAppearance, ReleaseStretchKeepsTheDrawnBottomOnTheFloor)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 2.5f);
    ASSERT_NE(player, nullptr);
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(rock, nullptr);
    for (int frame = 0; frame < 60; ++frame)
    {
        player->Update(false);
        rock->Update();
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    bool released = false;
    for (int frame = 0; frame < 30 && !released; ++frame)
    {
        released = StepAndSeeTheRelease(*player, *rock);
    }
    ASSERT_TRUE(released);
    ASSERT_FALSE(player->Resolver().Answer().lastImpact.broke);
    ASSERT_TRUE(player->ModelClassObj()->IsDrawingShape());
    NS::Model* model = player->ModelClassObj();
    // 試しには mesh が無いので、組み込みの sphere (半径 0.5) の局所の境界を差し、回転を外して形の伸びだけを測る
    // 玉の倍率は当たりの半径に合わせてあるので、描く玉の下端は当たりの下端に乗る
    const float radius = player->Collider().CapsuleRadius();
    NS::AABB local{};
    local.Center = NS::Vector3{0.0f, 0.0f, 0.0f};
    local.Extents = NS::Vector3{0.5f, 0.5f, 0.5f};
    model->SetLocalBoundsOverride(local);
    model->SnapLocalRotation(NS::Quaternion::Identity);
    NS::AABB drawn{};
    local.Transform(drawn, model->DrawWorldMatrix(1.0f));
    const float bottom = drawn.Center.y - drawn.Extents.y;
    EXPECT_NEAR(bottom, player->Root().Position().y - radius, 0.001f);
    EXPECT_GE(bottom, -0.001f);
}

// 当たりの形の再生の間は構えを混ぜない。溜めを押したまま当てても、描く倍率は押していない当たりと同じ
TEST(PlayerAppearance, TheHitShapeDoesNotMixTheStance)
{
    // 通常突進で当て、当たりの状態に入った歩から 12 歩の描く倍率を並べる
    const auto drawScalesOfTheHit = [](bool held, bool& stanceShown) {
        std::vector<NS::Vector3> scales;
        NS::Scene scene;
        PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
        EXPECT_NE(player, nullptr);
        GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
        EXPECT_NE(rock, nullptr);
        if (player == nullptr || rock == nullptr)
        {
            return scales;
        }
        player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 40 && scales.size() < 12; ++frame)
        {
            player->Update(held);
            rock->Update();
            if (!player->IsInTackleHit())
            {
                continue;
            }
            EXPECT_TRUE(player->ModelClassObj()->IsDrawingShape());
            stanceShown = stanceShown || player->StanceHeight() < 1.0f;
            scales.push_back(player->ModelClassObj()->GetDrawScale());
        }
        return scales;
    };
    bool releasedStance = false;
    bool heldStance = false;
    const std::vector<NS::Vector3> released = drawScalesOfTheHit(false, releasedStance);
    const std::vector<NS::Vector3> held = drawScalesOfTheHit(true, heldStance);
    ASSERT_EQ(released.size(), 12u);
    ASSERT_EQ(held.size(), released.size());
    ASSERT_FALSE(releasedStance);
    ASSERT_TRUE(heldStance) << "押している当たりは構えの縮みを持つ";
    for (std::size_t k = 0; k < held.size(); ++k)
    {
        SCOPED_TRACE(k);
        ExpectSameVector(held[k], released[k]);
    }
}

// 編集の休止の持ち主は世界の駆動だけ。身体と入力の部品を起こしたままでも、止めた世界では自機が動かない
TEST(PlayerUpdatePipeline, StoppedWorldHoldsThePlayerWithItsPartsAwake)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    scene.SetSimulationEnabled(false);
    player->Input().SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    // 入力の段が実機の入力で歩きを消しても、回っていればこの速さで動く
    player->Body().SetVelocity(NS::Vector3{0.0f, 2.0f, 0.0f});
    const NS::Vector3 position = player->Root().Position();

    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }

    ExpectSameVector(player->Root().Position(), position);
}

// 溜めて当てる組と通常突進の組を、本番の 1 フレームの入口で回した数字の基準
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
            [100, -0.0123793595, 0.6500000954, 1.3523943424, -0.8675079346, 1.67509e-7, 0.104101181, 0.14, 0.5, 2.56],
            [110, -0.02943230793, 1.116387725, 1.210286379, -0.2557942271, 6.683314323, -2.131618738, 0.14, 1.384893417, 5.983239174],
            [140, -0.1573294252, 2.848669291, 0.1444766968, -0.2557942271, 0.641646266, -2.131618738, 0.14, 4.491162777, 32.10754395],
            [189, -0.294336319, 1.149999738, -0.9972489476, 0, 0, 0, 0.14, 0.501000941, 74.210495]
        ], [
            [0, "Idle", 1, 0.9700000286, 1, true],
            [11, "Idle", 1, 0.9499999881, 1, true],
            [90, "BodySlam", 1, 1, 1, true],
            [94, "Walk", 1, 1, 1, true],
            [95, "TackleHit", 0.994802475, 1.100000024, 0.7051975131, false],
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
            [30, 0, 1.669279456, 1.836697578, 0, 1.664880991, -0.5998571515, 0, 0.8259615898, 10.53560448],
            [60, 0, 1.149999976, 1.53676939, 0, 0, -0.5998571515, -1.636946321, 1.185097575, 25.45308876],
            [89, 0, 1.149999738, 1.53676939, 0, 0, 0, -3.877855778, 0.5009999275, 37.27786636]
        ], [
            [0, "Idle", 1, 0.9700000286, 1, true],
            [3, "BodySlam", 1, 1, 1, true],
            [14, "Fall", 1, 1, 1, true],
            [15, "TackleHit", 1, 1.100000024, 0.6999999881, false],
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
        // 止めの長さは移す前の式が出した長さ (溜め 12・通常突進 3) を、移す前の返りを写した段の束に置く
        int legacyStopSteps = 3;
        if (charge)
        {
            legacyStopSteps = 12;
        }
        const ScopedActorFileEdit legacySets(
            "PlayerActor", [legacyStopSteps](nlohmann::json& actorFile) { UseLegacyHitSets(actorFile, legacyStopSteps); });
        NS::Scene scene;
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
        PlayerActor* player = PlacePipelinePlayer(scene, targetX, targetZ);
        ASSERT_NE(player, nullptr);
        // 基準は通常突進の初速 10 で、紫の揺れと紫の威力が無い時に採った
        // 外れの反動は真ん中と同じ高さと距離で、着いた後にこすらない。外れの相手は押し込む成分だけで飛ぶ距離を縮める
        // 止めの間の置物の位置は、根を往復させていた頃から食い込みの距離だけの値へ直した
        // 置物は自分の束 (PropActor.actor) の止めの長さだけ止まるので、通常突進の組の置物は自機の止め 3 より長く止まってから飛ぶ
        // 溜めは 1.5 秒押して紫に入ってから放す
        // 溜めの組の止めの間の潰れは、突進の向きでなく自機と置物のセンサーの水平の向きで横と奥に分ける。
        // 置物を横へずらしてから当てるので、横の倍率も 1 から少し縮む
        // 見るのは状態の移り方なので、欄の既定を触っても基準を取り直さずに済むよう留める
        EXPECT_EQ(NS::ApplyJsonFields(player->Params(),
                                      nlohmann::json{{"通常突進の初速", 10.0f},
                                                     {"紫の揺れの最大のずれ", 0.0f},
                                                     {"紫の威力の上限", 1.0f},
                                                     {"外れの反動の高さの割合", 1.0f},
                                                     {"外れの反動の距離の倍率", 1.0f},
                                                     {"叩きつけた時の跳ね", 1.0f},
                                                     {"外れのこすって止まるまでのフレーム数", 0},
                                                     {"中心近くの当たりの反動の距離の倍率", 2.0f},
                                                     {"中心近くの当たりの反動の高さの倍率", 1.0f}}),
                  0u);
        GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        EXPECT_EQ(NS::ApplyJsonFields(rock->Params(), nlohmann::json{{"外れで飛ばす相手の距離の割合", 1.0f}}), 0u);
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
                rock->Root().ShiftPosition(NS::Vector3{0.1f, 0.0f, 0.0f});
            }
            const bool wasInHit = player->IsInTackleHit();
            player->Update(held);
            if (impact < 0 && player->Resolver().Answer().lastImpact.sequence > 0)
            {
                impact = frame;
            }
            if (freeze < 0 && player->IsInTackleHit())
            {
                freeze = frame;
            }
            if (release < 0 && wasInHit && !player->IsInTackleHit())
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
                    player->ModelClassObj()->GetDrawScale(),
                    NS::Vector3{(*pose)[2].get<float>(), (*pose)[3].get<float>(), (*pose)[4].get<float>()});
                // 構え・潰れ・伸び・着地の潰れのどれの間も、根のスケールは配置の値のまま
                EXPECT_TRUE(player->Root().Scale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
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
                ExpectSameVector(player->Root().Position(),
                                 NS::Vector3{sample[1].get<float>(), sample[2].get<float>(), sample[3].get<float>()});
                ExpectSameVector(player->Body().Velocity(),
                                 NS::Vector3{sample[4].get<float>(), sample[5].get<float>(), sample[6].get<float>()});
                ExpectSameVector(rock->Root().Position(),
                                 NS::Vector3{sample[7].get<float>(), sample[8].get<float>(), sample[9].get<float>()});
            }
        }
        EXPECT_EQ(impact, expected[1].get<int>());
        EXPECT_EQ(freeze, expected[2].get<int>());
        EXPECT_EQ(release, expected[3].get<int>());
    }
}
