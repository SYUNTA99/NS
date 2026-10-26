#include "Editor/EditorActors.h"
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
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace
{
    // 裁定の観測と決定を、自機の段と同じ引数で単独に呼ぶ。答えにある自機を動かす事はしない
    void ObserveImpact(PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "ObserveImpact: 渡したアクターか部品が無い");
        player->Resolver().ObserveImpact(player->SelfForImpact(), player->SlamMotion(), &player->Collider());
    }

    void StepImpact(PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "StepImpact: 渡したアクターか部品が無い");
        player->Resolver().StepState(player->SelfForImpact(),
                                    player->SlamMotion(),
                                    &player->Params(),
                                    &player->Body(),
                                    &player->Collider(),
                                    player->HitReactionClassObj());
    }

    PlayerActor* PlacePipelinePlayer(NS::Scene& scene, float targetX = 0.0f, float targetZ = 3.0f)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeActorJson();
        NS::SetActorJsonFile(player, "PlayerActor");
        NS::SetActorJsonId(player, 1);
        NS::Editor::SetActorJsonPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, "PropActor");
        NS::SetActorJsonId(rock, 2);
        NS::Editor::SetActorJsonPosition(rock, NS::Vector3{targetX, 0.5f, targetZ});
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    // 見本の列に書く状態の綴り。表に無い状態は Other
    std::string_view StateName(const PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "StateName: 渡したアクターか部品が無い");
        if (player->States().IsCurrent<GL::Player::IdlePlayerState>())
        {
            return "Idle";
        }
        if (player->States().IsCurrent<GL::Player::WalkPlayerState>())
        {
            return "Walk";
        }
        if (player->States().IsCurrent<GL::Player::BrakePlayerState>())
        {
            return "Brake";
        }
        if (player->States().IsCurrent<GL::Player::FallPlayerState>())
        {
            return "Fall";
        }
        if (player->States().IsCurrent<GL::Player::BodySlamPlayerState>())
        {
            return "BodySlam";
        }
        if (player->States().IsCurrent<GL::Player::BeforeContactPlayerState>())
        {
            return "BeforeContact";
        }
        if (player->States().IsCurrent<GL::Player::TackleHitPlayerState>())
        {
            return "TackleHit";
        }
        if (player->States().IsCurrent<GL::Player::ReboundPlayerState>())
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
    bool SlamIntoTheRock(PlayerActor* player, GL::Level::PropActor* rock, bool waitForFreeze)
    {
        NS_ASSERT(Game, player != nullptr && rock != nullptr, "SlamIntoTheRock: 渡したアクターか部品が無い");
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 10; ++frame)
        {
            player->Update(false);
            rock->Update();
            if (waitForFreeze && player->IsInTackleHit())
            {
                return true;
            }
            if (!waitForFreeze && player->Resolver().Answer().lastImpact.sequence > 0)
            {
                return true;
            }
        }
        return false;
    }

    // 1 歩回し、当たりの状態に居た自機がその歩で出た (止めが明けた) 場合 true
    bool StepAndSeeTheRelease(PlayerActor* player, GL::Level::PropActor* rock)
    {
        NS_ASSERT(Game, player != nullptr && rock != nullptr, "StepAndSeeTheRelease: 渡したアクターか部品が無い");
        const bool wasInHit = player->IsInTackleHit();
        player->Update(false);
        rock->Update();
        return wasInHit && !player->IsInTackleHit();
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
    ObserveImpact(player);
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
    ObserveImpact(player);
    StepImpact(player);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.sequence, 1u);
    ASSERT_FALSE(player->Resolver().Answer().beginHit);
    StepImpact(player);
    EXPECT_FALSE(player->Resolver().Answer().beginHit);
    EXPECT_TRUE(player->Resolver().IsHitAwaitingEntry());
    EXPECT_EQ(player->Resolver().Answer().lastImpact.sequence, 1u);
    ObserveImpact(player);
    StepImpact(player);
    EXPECT_TRUE(player->Resolver().Answer().beginHit);
}

TEST(PlayerUpdatePipeline, RemovingTheObservedTargetCannotApplyAStaleImpact)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    ObserveImpact(player);
    scene.Actors().RemoveByActorId(2);
    StepImpact(player);
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
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(player, rock, true));
    EXPECT_TRUE(player->IsHitStopping());
    EXPECT_FALSE(player->CanMoveBody());
    const NS::Vector3 position = player->Root().Position();
    const NS::uint32 stateStep = player->States().StepCount();
    // 止めが明けて反動へ移る歩までは止まったまま
    bool released = false;
    for (int frame = 0; frame < 30 && !released; ++frame)
    {
        released = StepAndSeeTheRelease(player, rock);
        if (released)
        {
            break;
        }
        SCOPED_TRACE(frame);
        ExpectSameVector(player->Root().Position(), position);
        EXPECT_EQ(player->States().StepCount(), stateStep);
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
        GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
        ASSERT_NE(rock, nullptr);
        const NS::Vector3 rockHome = rock->Root().Position();
        ASSERT_TRUE(SlamIntoTheRock(player, rock, waitForFreeze));
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
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(player, rock, true));
    ASSERT_TRUE(player->ModelClassObj()->IsDrawingShape());
    ASSERT_LT(player->ModelClassObj()->GetDrawScale().z, 1.0f);
    player->OnEndPlay();
    EXPECT_TRUE(player->ModelClassObj()->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(player->IsHitStopping());
    EXPECT_FALSE(player->IsInTackleHit());
    EXPECT_FALSE(player->ModelClassObj()->IsDrawingShape());
}

// 当てた瞬間の潰れと明けの伸びは描く形の倍率に出て、根のスケールは 1 のまま。戻しの最後のフレームでちょうど 1
// 止めの途中で、衝撃が背中に着いたフレームに横と高さを 1.3 倍へ一度膨らませ、5 フレームで潰れの形へ戻す
TEST(CollisionImpact, HitStopSquashIsDrawnAndTheRootStaysOne)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SlamIntoTheRock(player, rock, true));
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
        released = StepAndSeeTheRelease(player, rock);
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
    GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
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
        released = StepAndSeeTheRelease(player, rock);
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
    local.Transform(drawn, model->DrawWorldMatrix(player->Root().WorldMatrix(), 1.0f));
    const float bottom = drawn.Center.y - drawn.Extents.y;
    EXPECT_NEAR(bottom, player->Root().Position().y - radius, 0.001f);
    EXPECT_GE(bottom, -0.001f);
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

