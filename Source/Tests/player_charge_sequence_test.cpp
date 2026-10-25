#include "Game/Level/ImpactResolver.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Effect.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/Core/Windows/Clock.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestTackleScene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。PlayerActor::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    // +Z を向くカメラと一緒に置いた、床の無いシーンの自機
    PlayerActor* PlaceChargePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::SceneJsonActors(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        if (PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f}) == nullptr)
        {
            return nullptr;
        }
        return static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    }

    const NS::EffectRecord* LiveEffect(const PlayerActor* player, std::string_view name)
    {
        NS_ASSERT(Game, player != nullptr, "LiveEffect: 渡したアクターか部品が無い");
        for (const NS::EffectRecord& record : player->GetEffect()->Records())
        {
            if (record.name == name && !record.endStep.has_value())
            {
                return &record;
            }
        }
        return nullptr;
    }
} // namespace

// しきい値の前・溜めの間・溜めすぎの間のどのフレームでも、玉の回る軸・溜まる光の根の向き・次のフレームに放した突進の
// 向きが同じ。押したフレームは +X へ倒し、押している間は -X へ倒す。カメラは +Z を向く
TEST(PlayerChargeSequence, SpinAxisChargeEffectAndReleaseShareOneDirection)
{
    for (const float heldSeconds : {0.05f, 0.5f, 1.2f})
    {
        SCOPED_TRACE(heldSeconds);
        NS::Scene scene;
        PlayerActor* player = PlaceChargePlayer(scene);
        ASSERT_NE(player, nullptr);
        ASSERT_EQ(
            NS::ApplyJsonFields(&player->Params(),
                                {{"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}, {"溜めすぎの秒数", 0.5f}}),
            0u);
        player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
        player->Update(true);
        player->SetDesiredMove(NS::Vector3{-1.0f, 0.0f, 0.0f}, 1.0f);
        for (int held = 1; held < FramesFor(heldSeconds); ++held)
        {
            player->Update(true);
        }
        ASSERT_TRUE(player->ChargeJudge().IsHoldingCharge());
        const NS::Vector3 axis = player->Appearance().SpinAxis();
        const NS::EffectRecord* gather = LiveEffect(player, "charge.gather");
        ASSERT_NE(gather, nullptr);
        ASSERT_TRUE(gather->rotation.has_value());
        const NS::Quaternion gatherRotation = *gather->rotation;

        player->Update(false);
        ASSERT_TRUE(player->IsBodySlamming());
        const NS::Vector3 released = player->BodySlamDirection();
        // 玉は 軸 × 上 の向きへ倒れるので、出る向き (x, z) の軸は (z, −x)
        EXPECT_NEAR(axis.x, released.z, 0.0001f);
        EXPECT_NEAR(axis.z, -released.x, 0.0001f);
        const NS::Quaternion expected = GL::Player::ChargeEffects::YawToward(released);
        EXPECT_NEAR(gatherRotation.x, expected.x, 0.0001f);
        EXPECT_NEAR(gatherRotation.y, expected.y, 0.0001f);
        EXPECT_NEAR(gatherRotation.z, expected.z, 0.0001f);
        EXPECT_NEAR(gatherRotation.w, expected.w, 0.0001f);
    }
}

// 溜めている途中でやり直すと、溜め量は 0 で、押したフレームの狙いの控えも走る速さの倍率も残らない。
// 押したフレームは +X へ倒し、カメラは +Z を向く。やり直しで入力は消えるので、控えが無ければカメラの正面へ出る
TEST(PlayerChargeSequence, RestartWhileChargingDropsTheChargeTheAimAndTheSlowdown)
{
    NS::Scene scene;
    PlayerActor* player = PlaceChargePlayer(scene);
    ASSERT_NE(player, nullptr);
    // しきい値を狙いの巻き戻し秒より短くし、控えが効いている間に溜めへ入る
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), {{"チャージしきい値秒", 0.05f}, {"狙いの巻き戻し秒", 0.11f}}),
              0u);
    const float runMaxSpeed = player->MaxSpeed();
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int held = 0; held < FramesFor(0.08f); ++held)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    ASSERT_GT(player->ChargeJudge().Charge01(), 0.0f);
    ASSERT_LT(player->MaxSpeed(), runMaxSpeed);

    player->Restart();
    EXPECT_FALSE(player->ChargeJudge().IsHeld());
    EXPECT_FLOAT_EQ(player->ChargeJudge().Charge01(), 0.0f);
    EXPECT_FLOAT_EQ(player->MaxSpeed(), runMaxSpeed);
    const NS::Vector3 release = player->ReleaseDirection();
    EXPECT_NEAR(release.x, 0.0f, 0.0001f);
    EXPECT_NEAR(release.z, 1.0f, 0.0001f);
    // 押さずに進めても、やり直しの前の溜めが放されて突進が出る事は無い
    player->Update(false);
    EXPECT_FALSE(player->IsBodySlamming());
}

// 押したフレームに丸まり、しきい値のフレームに減速して横を止め、放したフレームに発動する
TEST(PlayerChargeSequence, PressThresholdAndReleaseKeepTheSameFrameOrder)
{
    NS::Scene scene;
    PlayerActor* placed = PlaceChargePlayer(scene);
    ASSERT_NE(placed, nullptr);
    PlayerActor& player = *placed;
    ASSERT_EQ(NS::ApplyJsonFields(&player.Params(), {{"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}}), 0u);
    const int threshold = FramesFor(0.2f);
    const int full = FramesFor(1.0f);
    NS::Body& movement = player.Body();
    movement.SetVelocity(NS::Vector3{3.0f, 5.0f, 4.0f});
    const float maxSpeed = player.MaxSpeed();

    player.Update(true);
    int held = 1;
    EXPECT_TRUE(player.ChargeJudge().JustPressed());
    EXPECT_FALSE(player.ChargeJudge().IsCharging());
    EXPECT_TRUE(player.IsCurled());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_GT(movement.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.97f);

    while (held < threshold - 1)
    {
        player.Update(true);
        ++held;
        EXPECT_FALSE(player.ChargeJudge().IsCharging());
    }
    movement.SetVelocity(NS::Vector3{3.0f, 5.0f, 4.0f});
    player.Update(true);
    ++held;
    EXPECT_TRUE(player.ChargeJudge().JustStartedCharging());
    EXPECT_NEAR(player.MaxSpeed(), maxSpeed * 0.3f, 0.00001f);
    EXPECT_FLOAT_EQ(movement.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(movement.Velocity().z, 0.0f);
    // 縦は残す。0 を書くと同じフレームの重力で下向きになる
    EXPECT_GT(movement.Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.95f);

    // 横を止めるのはしきい値のフレームだけ
    movement.SetVelocity(NS::Vector3{2.0f, 5.0f, 1.0f});
    player.Update(true);
    ++held;
    EXPECT_GT(movement.Velocity().x, 0.0f);
    while (held < (threshold + full) / 2)
    {
        player.Update(true);
        ++held;
    }
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), 0.5f);
    player.Update(false);
    EXPECT_FALSE(player.ChargeJudge().IsHeld());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.5f);
}

TEST(PlayerChargeSequence, TapAndRepeatedPressDoNotSkipOrDuplicateAFrame)
{
    NS::Scene scene;
    PlayerActor* placed = PlaceChargePlayer(scene);
    ASSERT_NE(placed, nullptr);
    PlayerActor& player = *placed;
    ASSERT_EQ(NS::ApplyJsonFields(&player.Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    const int threshold = FramesFor(0.2f);
    player.Update(true);
    player.SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.Update(false);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);
    player.ResetState();
    player.Update(false);
    EXPECT_FALSE(player.IsBodySlamming());
    player.Update(true);
    EXPECT_TRUE(player.ChargeJudge().JustPressed());
    EXPECT_FALSE(player.ChargeJudge().IsCharging());
    for (int held = 2; held < threshold; ++held)
    {
        player.Update(true);
        EXPECT_FALSE(player.ChargeJudge().IsCharging());
    }
    player.Update(true);
    EXPECT_TRUE(player.ChargeJudge().JustStartedCharging());
}

TEST(PlayerChargeSequence, LiveTimingAppliesAndRestartStartsTheHeldDurationOver)
{
    NS::Scene scene;
    PlayerActor* placed = PlaceChargePlayer(scene);
    ASSERT_NE(placed, nullptr);
    PlayerActor& player = *placed;
    GL::Player::PlayerParams* params = &player.Params();
    player.Update(true);
    player.Restart();
    EXPECT_FALSE(player.IsCurled());
    ASSERT_EQ(NS::ApplyJsonFields(params, {{"チャージしきい値秒", 0.1f}, {"チャージ満タン秒", 0.3f}}), 0u);
    // 出直しの前に押した 1 フレームは数えず、出直した後の押しからしきい値と満タンの真ん中まで押す
    const int threshold = FramesFor(0.1f);
    const int full = FramesFor(0.3f);
    for (int held = 0; held < (threshold + full) / 2; ++held)
    {
        player.Update(true);
    }
    EXPECT_TRUE(player.IsCurled());
    EXPECT_TRUE(player.ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), 0.5f);
    player.SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.Update(false);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.5f);
}

// 押したまま出直すと、出直した後の押しから溜め直す。消えて出直すだけ (Die と Appear) では溜めは途切れない
TEST(PlayerChargeSequence, ChargeStartsOverAtARestartAndCountsThroughADisappear)
{
    NS::Scene scene;
    PlayerActor* placed = PlaceChargePlayer(scene);
    ASSERT_NE(placed, nullptr);
    PlayerActor& player = *placed;
    GL::Player::PlayerParams* params = &player.Params();
    ASSERT_EQ(NS::ApplyJsonFields(params, {{"チャージしきい値秒", 0.1f}, {"チャージ満タン秒", 1.0f}}), 0u);
    // 溜め量は (押したフレーム数 − しきい値) / (満タン − しきい値)
    const int threshold = FramesFor(0.1f);
    const int full = FramesFor(1.0f);
    const float span = static_cast<float>(full - threshold);
    for (int frame = 0; frame < threshold * 3; ++frame)
    {
        player.Update(true);
    }
    player.Restart();
    for (int frame = 0; frame < threshold * 2; ++frame)
    {
        player.Update(true);
    }
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), static_cast<float>(threshold) / span);
    player.Die();
    player.Appear();
    for (int frame = 0; frame < threshold * 2; ++frame)
    {
        player.Update(true);
    }
    EXPECT_TRUE(player.ChargeJudge().IsHeld());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), static_cast<float>(threshold * 3) / span);
}

// 構えは縦の倍率を答えるだけで、根のスケールは押しても溜めても放しても配置の値のまま
TEST(PlayerChargeSequence, StanceLeavesTheRootScaleAndAnswersTheHeight)
{
    NS::Scene scene;
    PlayerActor* placed = PlaceChargePlayer(scene);
    ASSERT_NE(placed, nullptr);
    PlayerActor& player = *placed;
    ASSERT_EQ(NS::ApplyJsonFields(&player.Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    const NS::Vector3 one{1.0f, 1.0f, 1.0f};
    player.Update(true);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.97f);
    EXPECT_TRUE(player.Root().Scale() == one);
    for (int held = 1; held < FramesFor(0.2f); ++held)
    {
        player.Update(true);
        SCOPED_TRACE(held);
        EXPECT_TRUE(player.Root().Scale() == one);
    }
    ASSERT_TRUE(player.ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.95f);
    player.Update(false);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_TRUE(player.Root().Scale() == one);
}

TEST(PlayerChargeSequence, EndPlayWhileHeldRestoresStanceAndUncurls)
{
    NS::Scene scene;
    PlayerActor* placed = PlaceChargePlayer(scene);
    ASSERT_NE(placed, nullptr);
    PlayerActor& player = *placed;
    ASSERT_EQ(NS::ApplyJsonFields(&player.Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player.Update(true);
    }
    ASSERT_FLOAT_EQ(player.ModelClassObj()->GetDrawScale().y, 0.95f);
    player.OnEndPlay();
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_TRUE(player.ModelClassObj()->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(player.IsCurled());
}

TEST(PlayerChargeSequence, ChargedReleaseUsesTheLastHeldAimBeforeClearingIt)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json entry = NS::MakeActorJson();
    NS::SetActorJsonFile(entry, "PlayerActor");
    NS::SetActorJsonId(entry, 1);
    NS::SceneJsonActors(doc).push_back(std::move(entry));
    scene.ResetFromJson(doc);
    PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    TestViewCamera* camera = PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_NE(camera, nullptr);
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    GL::Level::AimLine aim{};
    ASSERT_TRUE(player->TryGetAimLine(aim));
    EXPECT_FLOAT_EQ(aim.direction.z, 1.0f);
    camera->SetPose(NS::Vector3{}, NS::Vector3{1.0f, 0.0f, 0.0f});
    player->Update(false);
    EXPECT_FALSE(player->TryGetAimLine(aim));
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FLOAT_EQ(player->BodySlamDirection().x, 0.0f);
    EXPECT_FLOAT_EQ(player->BodySlamDirection().z, 1.0f);
}

// 狙いの線は遊びの視点 (仮想カメラの合成) に沿う。描画が書いた実カメラの向きは読まない
TEST(PlayerChargeSequence, AimLineFollowsTheViewCameraNotTheDrawnCamera)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json entry = NS::MakeActorJson();
    NS::SetActorJsonFile(entry, "PlayerActor");
    NS::SetActorJsonId(entry, 1);
    NS::SceneJsonActors(doc).push_back(std::move(entry));
    scene.ResetFromJson(doc);
    PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{-5.0f, 0.0f, 0.0f}, NS::Vector3{}), nullptr);
    NS::SceneCamera* camera = scene.MainCamera();
    ASSERT_NE(camera, nullptr);
    camera->SetPosition(NS::Vector3{});
    camera->SetTarget(NS::Vector3{0.0f, 0.0f, 1.0f});
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());

    GL::Level::AimLine aim{};
    ASSERT_TRUE(player->TryGetAimLine(aim));
    EXPECT_FLOAT_EQ(aim.direction.x, 1.0f);
    EXPECT_FLOAT_EQ(aim.direction.z, 0.0f);
}

namespace
{
    // 放しの弾けの記録。出していなければ nullptr
    const NS::EffectRecord* ReleaseBurstOf(const PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "ReleaseBurstOf: 渡したアクターか部品が無い");
        const NS::EffectRecord* burst = nullptr;
        for (const NS::EffectRecord& record : player->GetEffect()->Records())
        {
            if (record.name == "release.burst")
            {
                burst = &record;
            }
        }
        return burst;
    }

    // 置物へ +z の突進を出し、検知の歩まで回す。検知の歩と、当たる直前に入った歩の、演出の記録の歩を返す
    struct BurstRun
    {
        int contactStep = -1;
        int beforeContactStep = -1;
    };

    BurstRun RunBurstUntilContact(NS::Scene& scene, PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "RunBurstUntilContact: 渡したアクターか部品が無い");
        BurstRun run;
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 120 && run.contactStep < 0; ++frame)
        {
            scene.OnUpdate();
            if (run.beforeContactStep < 0 && player->IsInBeforeContact())
            {
                run.beforeContactStep = static_cast<int>(player->GetEffect()->Step());
            }
            if (player->Resolver().Answer().lastImpact.sequence != 0)
            {
                run.contactStep = static_cast<int>(player->GetEffect()->Step());
            }
        }
        return run;
    }
} // namespace

// 当たる直前の状態に入ったフレームに放しの弾けを消し、縮みと止まる間を光で隠さない
TEST(PlayerChargeSequence, ReleaseBurstEndsWhenTheBeforeContactStateStarts)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const BurstRun run = RunBurstUntilContact(scene, player);
    ASSERT_GE(run.contactStep, 0);
    ASSERT_GE(run.beforeContactStep, 0);
    const NS::EffectRecord* burst = ReleaseBurstOf(player);
    ASSERT_NE(burst, nullptr);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(static_cast<int>(*burst->endStep), run.beforeContactStep);
}

// 自機に当たる直前の動作が 1 つも無ければ線の先を予測しないので、触れたフレームに放しの弾けを消す
TEST(PlayerChargeSequence, ReleaseBurstEndsOnTheContactFrameWithoutABeforeContactAction)
{
    const ScopedActorFileEdit noActions("PlayerActor", [](nlohmann::json& actorFile) {
        NS::WriteActorActions(actorFile, std::vector<NS::ActorAction>{});
    });
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const BurstRun run = RunBurstUntilContact(scene, player);
    ASSERT_GE(run.contactStep, 0);
    EXPECT_LT(run.beforeContactStep, 0);
    const NS::EffectRecord* burst = ReleaseBurstOf(player);
    ASSERT_NE(burst, nullptr);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(static_cast<int>(*burst->endStep), run.contactStep);
}

// 触れる見込みが 3 フレーム以内になったら放しの弾けを消す。当たる直前に入らない外れの当たりでも、触れる直前を光で飾らない
TEST(PlayerChargeSequence, ReleaseBurstEndsThreeFramesBeforeThePredictedContact)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.75f);
    ASSERT_NE(player, nullptr);
    const BurstRun run = RunBurstUntilContact(scene, player);
    ASSERT_GE(run.contactStep, 0);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Wide);
    EXPECT_LT(run.beforeContactStep, 0);
    const NS::EffectRecord* burst = ReleaseBurstOf(player);
    ASSERT_NE(burst, nullptr);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(static_cast<int>(*burst->endStep), run.contactStep - 3);
}
