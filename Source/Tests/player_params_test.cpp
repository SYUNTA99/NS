#include "Editor/EditorActors.h"
#include "Game/Level/Health.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

TEST(PlayerParams, MaxHealthBelongsToPlayer)
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
    // 体力は .actor だけが持つ欄なので、読んだ後のアクターへ書いて満たす
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"体力", 5}}), 0u);
    player->ResetHealth();
    EXPECT_EQ(player->Health(), 5);
    player->ApplyDamage(2);
    EXPECT_EQ(player->Health(), 3);
    player->ResetHealth();
    EXPECT_EQ(player->Health(), 5);

    // エディタで体力を書き換えてから再生すると、満タンは書き換えた値になる
    GL::Player::PlayerParams* params = &player->Params();
    ASSERT_EQ(NS::ApplyJsonFields(params, nlohmann::json{{"体力", 3}}), 0u);
    player->ResetHealth();
    EXPECT_EQ(player->Health(), 3);
}

TEST(PlayerParams, DeactivatingPlayerKeepsHealth)
{
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    player.ResetHealth();
    const int full = player.Health();

    // 配置を止めて戻すのは死ではない。命は減らない
    player.Disappear();
    EXPECT_FALSE(player.IsAppeared());
    EXPECT_FALSE(player.IsDead());
    player.Appear();
    EXPECT_EQ(player.Health(), full);

    player.Die();
    EXPECT_TRUE(player.IsDead());
    EXPECT_FALSE(player.IsAppeared());
}

TEST(PlayerParams, MovementDefaultsKeepEveryDisplayNameAndValue)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const GL::Player::PlayerParams* params = &player.Params();
    const nlohmann::json fields = NS::SerializeClassObjFields(params);
    const nlohmann::json expected = {{"ジャンプ初速", 12.0f},
                                     {"上昇重力", -25.0f},
                                     {"下降重力", -35.0f},
                                     {"頂点滞空 Vy", 1.0f},
                                     {"頂点滞空倍率", 0.5f},
                                     {"ジャンプ離し倍率", 0.6f},
                                     {"コヨーテ時間", 0.025f},
                                     {"先行入力時間", 0.25f},
                                     {"歩き速度", 4.0f},
                                     {"走行速度", 8.0f},
                                     {"加速度", 40.0f},
                                     {"空中の加速度", 40.0f},
                                     {"曲がる時の抵抗", 40.0f},
                                     {"手を放した時の減速度", 40.0f},
                                     {"ブレーキの減速度", 40.0f},
                                     {"ブレーキのしきい値", -0.8f},
                                     {"スティック遊び", 0.3f},
                                     {"登れる段の高さ", 0.25f},
                                     {"掴める縁の下向き距離", 0.5f},
                                     {"縁へ手を伸ばす距離", 0.3f},
                                     {"よじ登りの所要時間", 0.25f},
                                     {"縁の横移動速度", 2.0f},
                                     {"振り向きの速さ", 970.0f},
                                     {"チャージ突進の速度", 20.0f},
                                     {"チャージ突進の距離", 10.0f},
                                     {"放つ角度の上限", 40.0f},
                                     {"通常突進の初速", 15.0f},
                                     {"通常突進の上向き初速", 3.0f},
                                     {"通常突進の距離", 6.25f},
                                     {"狙いの巻き戻し秒", 0.11f},
                                     {"狙いの巻き戻しが消える秒", 0.19f},
                                     {"反動の上りの重力倍率", 0.5f},
                                     {"反動中の空中の加速度", 2.0f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    // 左右の寄せは消した。鍵が戻ると、保存した場面に効かない角度が載る
    EXPECT_FALSE(fields.contains("寄せる角度の上限"));
    EXPECT_FALSE(fields.contains("1 フレームの向きの変化の上限"));
    const NS::Body* movement = &player.Body();
    // 身体は欄を持たない。動きの調整値は Params、当たりの寸法は Collider に居る
    EXPECT_TRUE(NS::SerializeClassObjFields(movement).empty());
    const NS::Collider* collider = &player.Collider();
    const nlohmann::json colliderFields = NS::SerializeClassObjFields(collider);
    EXPECT_EQ(colliderFields.size(), 2u);
    EXPECT_TRUE(colliderFields.contains("半径"));
    EXPECT_TRUE(colliderFields.contains("半分の高さ"));
}

TEST(PlayerParams, LiveTuningDrivesMovementWithoutCopiedValues)
{
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    GL::Player::PlayerParams* params = &player.Params();
    NS::Body* movement = &player.Body();
    EXPECT_EQ(NS::ApplyJsonFields(params, {{"走行速度", 10.0f}, {"加速度", 7.0f}, {"上昇重力", -15.0f}}), 0u);
    EXPECT_FLOAT_EQ(player.RunSpeed(), 10.0f);
    movement->SetGrounded(true);
    player.SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.AccelerateToInputDirection(0.1f);
    EXPECT_NEAR(movement->LateralVelocity().x, 0.7f, 0.00001f);
    movement->SetVerticalVelocity(2.0f);
    player.Gravity(0.1f);
    EXPECT_NEAR(movement->VerticalVelocity(), 0.5f, 0.00001f);
    EXPECT_EQ(NS::ApplyJsonFields(params, {{"走行速度", 11.0f}}), 0u);
    EXPECT_FLOAT_EQ(player.RunSpeed(), 11.0f);
}

// part の欄に expected の名前が全部あり、値も同じことを確かめる
static void ExpectClassObjFields(const NS::Object* part, const nlohmann::json& expected)
{
    NS_ASSERT(Game, part != nullptr, "ExpectClassObjFields: 渡したアクターか部品が無い");
    const nlohmann::json fields = NS::SerializeClassObjFields(part);
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
}

// 調整値の部品に expected の名前が 1 つも無いことを確かめる。演出の欄は描く部品が持ち、遊びの欄と並ばない
static void ExpectNotInParams(const PlayerActor* player, const nlohmann::json& expected)
{
    NS_ASSERT(Game, player != nullptr, "ExpectNotInParams: 渡したアクターか部品が無い");
    const nlohmann::json fields = NS::SerializeClassObjFields(&player->Params());
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        EXPECT_FALSE(fields.contains(it.key())) << it.key();
    }
}

TEST(PlayerParams, AppearanceDefaultsBelongToThePartThatDrawsThem)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const nlohmann::json appearance = {{"立ち姿のメッシュ", "capsule"},
                                       {"玉のメッシュ", "sphere"},
                                       {"溜め 0 の回る速さ", 360.0f},
                                       {"溜めきりの回る速さ", 1440.0f},
                                       {"着地の潰れ", 0.8f},
                                       {"着地の潰れを戻すフレーム数", 6},
                                       {"外れの縁でのねじれの回転数", 2.0f},
                                       {"外れで当たる前の回転を引き継ぐ割合", 0.4f},
                                       {"外れの回転を寄せるフレーム数", 6},
                                       {"外れの軸のぶれの角度", 10.0f},
                                       {"外れの軸のぶれの速さ", 1.5f}};
    // 回転数は PlayerActor が突進と反動の回る速さを出すのに読むので、調整値の部品に残る
    const nlohmann::json spin = {{"溜めた突進の届くまでの回転数", 5.0f},
                                 {"通常突進の届くまでの回転数", 2.0f},
                                 {"溜めて当てた反動の回転数", 3.0f},
                                 {"通常突進で当てた反動の回転数", 1.0f}};
    ExpectClassObjFields(&player.Appearance(), appearance);
    ExpectClassObjFields(&player.Params(), spin);
    ExpectNotInParams(&player, appearance);
}

TEST(PlayerParams, ImpactDefaultsKeepGameOwnedDisplayNamesAndValues)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const nlohmann::json fields = NS::SerializeClassObjFields(&player.Params());
    const nlohmann::json play = {{"反動の高さ", 1.15f},
                                 {"反動の距離", 0.575f},
                                 {"中心近くの当たりの反動の距離の倍率", 4.5f},
                                 {"中心近くの当たりの反動の高さの倍率", 0.6f},
                                 {"押し飛ばしの距離", 29.0f},
                                 {"押し飛ばしの質量指数", 0.35f},
                                 {"中心近くの当たりのヒットストップ倍率", 2.0f},
                                 {"ヒットストップの上限秒", 12.0f / 60.0f},
                                 {"破壊を許可", false},
                                 {"貫通時の減速倍率", 0.75f},
                                 {"貫通の止め秒", 4.0f / 60.0f}};
    ExpectClassObjFields(&player.Params(), play);
    // 相手の飛び方は MapObjParams の欄。自機の欄に置くと相手ごとに変えられない
    ExpectNotInParams(&player,
                      {{"押し飛ばしの高さ", 0},
                       {"押し飛ばしの上昇重力", 0},
                       {"下りの速さの倍率", 0},
                       {"頂点の帯の縦速度", 0},
                       {"頂点の帯の重力倍率", 0},
                       {"外れで飛ばす相手の弧の高さの割合", 0},
                       {"外れで飛ばす相手の距離の割合", 0}});
    const nlohmann::json expected = {{"輪をカメラへ起こす割合", 1.0f}};
    // カメラに対する輪の向きはGameが持ち、粒や光の調整値は素材が持つ
    ExpectClassObjFields(&player.ImpactVisuals(), expected);
    ExpectNotInParams(&player, expected);
    const nlohmann::json effectFields = NS::SerializeClassObjFields(&player.ImpactVisuals());
    EXPECT_FALSE(effectFields.contains("核の出始めの大きさ"));
    EXPECT_FALSE(effectFields.contains("外れの光の面積の落ち先"));
    EXPECT_FALSE(effectFields.contains("外れの光の面積の減衰"));
    EXPECT_FALSE(effectFields.contains("光条の細りきった太さ"));
    EXPECT_FALSE(effectFields.contains("輪の出始めの半径"));
    EXPECT_FALSE(effectFields.contains("照りが減り始めるフレーム"));
    EXPECT_FALSE(effectFields.contains("照りの山の前の面積の割合"));
    EXPECT_FALSE(effectFields.contains("輪が広がり切るフレーム"));
    EXPECT_FALSE(effectFields.contains("核の脈の倍率"));
    // 惜しいの段は消した。惜しいだけの演出の欄が戻ると、段の無い当たりの調整値が保存に載る
    // 当たりの返りの時間と形はタイムラインへ移した。欄が戻ると、同じ返りの値の出所が 2 つになる
    for (const char* removed : {"惜しい当たりの返りの割合",
                                "惜しい当たりの返りを引き始める割合",
                                "惜しいの輪が届く割合",
                                "惜しいの火花の数の割合",
                                "ヒットストップ基準秒",
                                "食い込み距離",
                                "振動の振幅",
                                "カメラ揺れの強さ",
                                "中心近くの当たりの揺れの倍率",
                                "大きな外れの揺れのフレーム数",
                                "大きな外れの揺れの縦と横の比",
                                "大きな外れの揺れの入れ替わりの最長フレーム数",
                                "中心近くの当たりの寄りの倍率",
                                "中心近くの当たりの傾き",
                                "寄りと傾きを戻すフレーム数",
                                "中心近くの当たりのパッドの振動の強さ",
                                "大きな外れのパッドの振動の強さ",
                                "潰れの厚み",
                                "潰れの伸び上がり",
                                "弾け伸びの倍率",
                                "弾け伸びの行き過ぎ",
                                "弾け伸びを戻すフレーム数",
                                "中心近くの当たりの白の濃さ",
                                "中心近くの当たりの白のフレーム数"})
    {
        EXPECT_FALSE(fields.contains(removed)) << removed;
    }
    EXPECT_TRUE(NS::SerializeClassObjFields(&player.Resolver()).empty());
}


TEST(PlayerParams, ImpactShapeTakesTheLookFromOneRowPerTier)
{
    using GL::Level::HitTier;
    using GL::Player::ImpactShape;
    using GL::Player::SparkHeading;
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    GL::Level::ImpactRecord impact{};
    impact.power = 1.0f;
    impact.hitStopSteps = 8;
    impact.targetPlaced = true;
    impact.tier = HitTier::Center;
    const ImpactShape center = player.ImpactVisuals().ShapeFor(impact);
    impact.tier = HitTier::Wide;
    const ImpactShape wide = player.ImpactVisuals().ShapeFor(impact);

    EXPECT_EQ(center.coreInput, 0u);
    EXPECT_EQ(center.sparkHeading, SparkHeading::Launch);
    EXPECT_TRUE(center.centerLayers);
    EXPECT_TRUE(center.groundLight);
    EXPECT_FALSE(center.coreCut);

    EXPECT_EQ(wide.coreInput, 2u);
    EXPECT_EQ(wide.sparkHeading, SparkHeading::Scrape);
    EXPECT_FALSE(wide.centerLayers);
    EXPECT_FALSE(wide.groundLight);
    EXPECT_TRUE(wide.coreCut);

    // 番号から作った段の外の値は、行を混ぜずに大きな外れの行で出す。中心近くの層は 1 つも足さない
    impact.tier = static_cast<HitTier>(3);
    const ImpactShape unknown = player.ImpactVisuals().ShapeFor(impact);
    EXPECT_EQ(unknown.coreInput, wide.coreInput);
    EXPECT_EQ(unknown.holdLastFrame, wide.holdLastFrame);
    EXPECT_EQ(unknown.sparkHeading, wide.sparkHeading);
    EXPECT_FALSE(unknown.centerLayers);
}

TEST(PlayerParams, LiveImpactTuningDrivesReboundAndLaunchRecord)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json entry = NS::MakeActorJson();
    NS::SetActorJsonFile(entry, "PlayerActor");
    NS::SetActorJsonId(entry, 1);
    NS::Editor::SetActorJsonPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
    NS::SceneJsonActors(doc).push_back(std::move(entry));
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::SetActorJsonId(rock, 2);
    // 自機の玉の中心 (根 1 m − 半分の高さ 0.5 m) と同じ高さ。赤の真ん中に当たり、威力の倍率は 1
    NS::Editor::SetActorJsonPosition(rock, NS::Vector3{0.0f, 0.5f, 1.0f});
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    PlayerActor* player = NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(player, nullptr);
    // 飛び方の高さは相手の欄に書く。.actor だけが持つ欄なので、読んだ後のアクターへ書く
    GL::Level::PropActor* target = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&target->Params(), nlohmann::json{{"押し飛ばしの高さ", 3.0f}}), 0u);
    EXPECT_FALSE(NS::SerializeClassObjFields(&player->Params()).contains("押し飛ばしの高さ"));
    EXPECT_EQ(NS::ApplyJsonFields(
                  &player->Params(),
                  {{"押し飛ばしの距離", 8.0f}, {"反動の高さ", 2.0f}, {"中心近くの当たりの反動の高さの倍率", 1.0f}}),
              0u);
    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Update(false);
    ASSERT_TRUE(player->Resolver().Answer().rebounded);
    const GL::Level::ImpactRecord& impact = player->Resolver().Answer().lastImpact;
    EXPECT_FLOAT_EQ(impact.launchDistance, 8.0f);
    EXPECT_FLOAT_EQ(impact.launchApexHeight, 3.0f);
    EXPECT_FLOAT_EQ(impact.reboundApexHeight, 2.0f);
}

TEST(PlayerParams, IndicatorDefaultsKeepAllThirtyFiveFields)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const GL::Level::TargetMarkerDesc marker{};
    const GL::Level::SlamArrowDesc arrow{};
    const nlohmann::json markerFields = {
        {"ロックオンの枠の色", {marker.color.x, marker.color.y, marker.color.z, marker.color.w}},
        {"ロックオンの枠の太さ", marker.lineThickness},
        {"ロックオンの枠の腕の割合", marker.armRatio},
        {"枠と輪郭の間", marker.frameGap},
        {"枠の一辺の下限", marker.frameMinSide},
        {"枠が出る時の倍率", marker.appearScale},
        {"枠が出る時の一辺の上限", marker.appearMaxSide},
        {"枠が縮むフレーム数", marker.appearFrames},
        {"枠が縮む進みの曲線",
         {{"curve", {{0.0f, 0.0f}, {0.25f, 0.12f}, {0.5f, 0.38f}, {0.75f, 0.70f}, {1.0f, 1.0f}}}}},
        {"枠が出る時の色", {marker.appearColor.x, marker.appearColor.y, marker.appearColor.z, marker.appearColor.w}},
        {"外れた時の枠の倍率", marker.lostScale},
        {"外れた時の枠のフレーム数", marker.lostFrames},
        {"枠の縁の色", {marker.outlineColor.x, marker.outlineColor.y, marker.outlineColor.z, marker.outlineColor.w}}};
    const nlohmann::json arrowFields = {
        {"矢印が伸びるフレーム数", arrow.growFrames},
        {"矢印を浮かせる高さ", arrow.groundLift},
        {"矢じりの幅", arrow.headWidth},
        {"矢じりの奥行きの割合", arrow.headDepthRatio},
        {"矢じりの奥行きの下限", arrow.headDepthMin},
        {"矢じりの奥行きの上限", arrow.headDepthMax},
        {"帯の始まりのぼかし", arrow.startFade},
        {"色の境目のぼかし", arrow.frontSoftness},
        {"後半の色へ変わる溜め量", arrow.lateStageFrom},
        {"溜めの前半の色", {arrow.earlyColor.x, arrow.earlyColor.y, arrow.earlyColor.z, arrow.earlyColor.w}},
        {"溜めの後半の色", {arrow.lateColor.x, arrow.lateColor.y, arrow.lateColor.z, arrow.lateColor.w}},
        {"溜めきりの色", {arrow.fullColor.x, arrow.fullColor.y, arrow.fullColor.z, arrow.fullColor.w}},
        {"色の付いていない部分の色", {arrow.plainColor.x, arrow.plainColor.y, arrow.plainColor.z, arrow.plainColor.w}},
        {"矢印の暗い縁の色", {arrow.darkColor.x, arrow.darkColor.y, arrow.darkColor.z, arrow.darkColor.w}},
        {"帯の明るい縁の不透明度", arrow.bandEdgeAlpha},
        {"帯の塗りの不透明度", arrow.bandFillAlpha},
        {"矢じりの明るい縁の不透明度", arrow.headEdgeAlpha},
        {"矢じりの塗りの不透明度", arrow.headFillAlpha},
        {"色の無い帯の明るい縁の不透明度", arrow.plainBandEdgeAlpha},
        {"色の無い帯の塗りの不透明度", arrow.plainBandFillAlpha},
        {"隠れた矢じりの不透明度", arrow.occludedHeadAlpha},
        {"矢じりを見せる最小の角度", arrow.headMinViewDegrees}};
    // 枠と矢印の見た目は描く部品が持ち、遊びの欄と並ばない
    ExpectClassObjFields(&player.TargetIndicator(), markerFields);
    ExpectClassObjFields(&player.SlamIndicator(), arrowFields);
    ExpectNotInParams(&player, markerFields);
    ExpectNotInParams(&player, arrowFields);
}

// 欄を別の部品へ移した後も、同梱の .actor と場面の値は読まれる。古い部品の下に残った鍵はどの欄にも
// 照合されず、詰めた値が静かに既定へ戻る
TEST(PlayerParams, ShippedAssetsKeepEveryFieldUnderThePartThatReadsIt)
{
    const auto expectEveryFieldRead = [](const nlohmann::json& entry, std::string_view source) {
        const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
        PlayerActor& player = *playerOwned;
        const nlohmann::json& parts = NS::ActorJsonClassObjs(entry);
        const nlohmann::json prototype = NS::Editor::MakePrototypeJson(&player);
        for (nlohmann::json::const_iterator it = parts.begin(); it != parts.end(); ++it)
        {
            EXPECT_NE(NS::ClassObjFields(prototype, it.key()), nullptr) << source << " " << it.key();
        }
        NS::ForEachReflectedClassObj(&player, [&](const NS::ClassObjInfo& field, NS::Object* part) {
            const nlohmann::json::const_iterator found = parts.find(field.key);
            if (found != parts.end())
            {
                EXPECT_EQ(NS::ApplyJsonFields(part, found.value()), 0u) << source << " " << field.key;
            }
        });
    };
    NS::ActorFileLibrary::Get().Reload();
    const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("PlayerActor");
    ASSERT_NE(actorFile, nullptr);
    expectEveryFieldRead(*actorFile, "PlayerActor.actor");

    for (const std::string_view sceneName : {"new_scene.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::SceneJsonActors(doc))
        {
            if (NS::ActorJsonFile(entry) == "PlayerActor")
            {
                expectEveryFieldRead(entry, sceneName);
            }
        }
    }
}
