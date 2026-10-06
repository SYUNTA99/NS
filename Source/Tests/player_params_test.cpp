#include "Game/Level/Health.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/Components/Body.h"
#include "NSlib/Object/Components/Collider.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/FileSystem.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

static_assert(!std::is_base_of_v<NS::Obj::Component, NS::Game::Level::Health>);

TEST(PlayerParams, MaxHealthBelongsToPlayer)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    entry["parts"] = {{"Params", {{"体力", 5}}}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_NE(NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player->Part("Params")), nullptr);
    EXPECT_EQ(player->Health(), 5);
    ASSERT_NE(NS::Obj::ComponentCast<NS::Obj::Body>(player->Part("Movement")), nullptr);
    player->ApplyDamage(2);
    EXPECT_EQ(player->Health(), 3);
    player->ResetHealth();
    EXPECT_EQ(player->Health(), 5);

    // エディタで体力を書き換えてから再生すると、満タンは書き換えた値になる
    NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player->Part("Params"));
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, nlohmann::json{{"体力", 3}}), 0u);
    player->ResetHealth();
    EXPECT_EQ(player->Health(), 3);
}

TEST(PlayerParams, DeactivatingPlayerKeepsHealth)
{
    Player player;
    player.ResetHealth();
    const int full = player.Health();

    // 配置を止めて戻すのは死ではない。命は減らない
    player.SetActive(false);
    EXPECT_FALSE(player.IsActiveSelf());
    EXPECT_FALSE(player.IsDead());
    player.SetActive(true);
    EXPECT_EQ(player.Health(), full);

    player.Die();
    EXPECT_TRUE(player.IsDead());
    EXPECT_FALSE(player.IsActiveSelf());
}

TEST(PlayerParams, MovementDefaultsKeepEveryDisplayNameAndValue)
{
    const Player player;
    const NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player.Part("Params"));
    ASSERT_NE(params, nullptr);
    const nlohmann::json fields = NS::Obj::SerializeComponent(*params)["fields"];
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
                                     {"突進速度", 20.0f},
                                     {"突進距離", 10.0f},
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
    const NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player.Part("Movement"));
    ASSERT_NE(movement, nullptr);
    // 身体は欄を持たない。動きの調整値は Params、当たりの寸法は Collider に居る
    EXPECT_TRUE(NS::Obj::SerializeComponent(*movement)["fields"].empty());
    const NS::Obj::Collider* collider = NS::Obj::ComponentCast<NS::Obj::Collider>(player.Part("Collider"));
    ASSERT_NE(collider, nullptr);
    const nlohmann::json colliderFields = NS::Obj::SerializeComponent(*collider)["fields"];
    EXPECT_EQ(colliderFields.size(), 2u);
    EXPECT_TRUE(colliderFields.contains("半径"));
    EXPECT_TRUE(colliderFields.contains("半分の高さ"));
}

TEST(PlayerParams, LiveTuningDrivesMovementWithoutCopiedValues)
{
    Player player;
    NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player.Part("Params"));
    NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player.Part("Movement"));
    ASSERT_NE(params, nullptr);
    ASSERT_NE(movement, nullptr);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(*params, {{"走行速度", 10.0f}, {"加速度", 7.0f}, {"上昇重力", -15.0f}}), 0u);
    EXPECT_FLOAT_EQ(player.RunSpeed(), 10.0f);
    movement->SetGrounded(true);
    player.SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.AccelerateToInputDirection(0.1f);
    EXPECT_NEAR(movement->LateralVelocity().x, 0.7f, 0.00001f);
    movement->SetVerticalVelocity(2.0f);
    player.Gravity(0.1f);
    EXPECT_NEAR(movement->VerticalVelocity(), 0.5f, 0.00001f);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(*params, {{"走行速度", 11.0f}}), 0u);
    EXPECT_FLOAT_EQ(player.RunSpeed(), 11.0f);
}

TEST(PlayerParams, SceneOverridesSurviveSaveAndReload)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    entry["parts"] = {
        {"Params", {{"走行速度", 9.0f}, {"突進距離", 14.0f}, {"上昇重力", -21.0f}, {"下降重力", -39.0f}, {"体力", 6}}}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    const nlohmann::json saved = scene.ToJson();
    scene.LoadJson(saved);
    const Player* player = static_cast<const Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    const NS::Obj::Body* movement = NS::Obj::ComponentCast<NS::Obj::Body>(player->Part("Movement"));
    ASSERT_NE(movement, nullptr);
    EXPECT_FLOAT_EQ(player->RunSpeed(), 9.0f);
    EXPECT_FLOAT_EQ(player->BodySlamDistance(), 14.0f);
    EXPECT_EQ(player->Health(), 6);
    const NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player->Part("Params"));
    ASSERT_NE(params, nullptr);
    const nlohmann::json fields = NS::Obj::SerializeComponent(*params)["fields"];
    EXPECT_FLOAT_EQ(fields["上昇重力"].get<float>(), -21.0f);
    EXPECT_FLOAT_EQ(fields["下降重力"].get<float>(), -39.0f);
}

// part の欄に expected の名前が全部あり、値も同じことを確かめる
static void ExpectPartFields(const NS::Obj::Component& part, const nlohmann::json& expected)
{
    const nlohmann::json fields = NS::Obj::SerializeComponent(part)["fields"];
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
}

// 調整値の部品に expected の名前が 1 つも無いことを確かめる。演出の欄は描く部品が持ち、遊びの欄と並ばない
static void ExpectNotInParams(const Player& player, const nlohmann::json& expected)
{
    const nlohmann::json fields = NS::Obj::SerializeComponent(player.Params())["fields"];
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        EXPECT_FALSE(fields.contains(it.key())) << it.key();
    }
}

TEST(PlayerParams, AppearanceAndChargeVisualDefaultsBelongToThePartsThatDrawThem)
{
    Player player;
    const nlohmann::json appearance = {{"立ち姿のメッシュ", ""},
                                       {"玉のメッシュ", ""},
                                       {"溜め 0 の回る速さ", 360.0f},
                                       {"溜めきりの回る速さ", 1440.0f},
                                       {"着地の潰れ", 0.8f},
                                       {"着地の潰れを戻すフレーム数", 6},
                                       {"外れの縁でのねじれの回転数", 2.0f},
                                       {"外れで当たる前の回転を引き継ぐ割合", 0.4f},
                                       {"外れの回転を寄せるフレーム数", 6},
                                       {"外れの軸のぶれの角度", 10.0f},
                                       {"外れの軸のぶれの速さ", 1.5f}};
    const nlohmann::json charge = {{"通常突進の弾けの大きさ", 0.75f},
                                   {"溜めきりで足す弾けの大きさ", 0.25f},
                                   {"紫の火花の数の始め", 4},
                                   {"紫の火花の数の終わり", 14},
                                   {"紫の火花の速さの始め", 2.5f},
                                   {"紫の火花の速さの終わり", 5.0f}};
    // 回転数は Player が突進と反動の回る速さを出すのに読むので、調整値の部品に残る
    const nlohmann::json spin = {{"溜めた突進の届くまでの回転数", 5.0f},
                                 {"通常突進の届くまでの回転数", 2.0f},
                                 {"溜めて当てた反動の回転数", 3.0f},
                                 {"通常突進で当てた反動の回転数", 1.0f}};
    ExpectPartFields(player.Appearance(), appearance);
    ExpectPartFields(player.ChargeVisuals(), charge);
    ExpectPartFields(player.Params(), spin);
    ExpectNotInParams(player, appearance);
    ExpectNotInParams(player, charge);
}

TEST(PlayerParams, LiveChargeVisualTuningKeepsClampingAndNonFiniteInput)
{
    Player player;
    EXPECT_EQ(NS::Obj::ApplyJsonFields(player.ChargeVisuals(),
                                       {{"通常突進の弾けの大きさ", 0.5f}, {"溜めきりで足す弾けの大きさ", 0.4f}}),
              0u);
    EXPECT_FLOAT_EQ(player.ChargeVisuals().ReleaseBurstScale(0.5f), 0.7f);
    EXPECT_FLOAT_EQ(player.ChargeVisuals().ReleaseBurstScale(-1.0f), 0.5f);
    EXPECT_FLOAT_EQ(player.ChargeVisuals().ReleaseBurstScale(2.0f), 0.9f);
    EXPECT_FLOAT_EQ(player.ChargeVisuals().ReleaseBurstScale(std::numeric_limits<float>::quiet_NaN()), 0.5f);
    EXPECT_FLOAT_EQ(player.ChargeVisuals().ReleaseBurstScale(std::numeric_limits<float>::infinity()), 0.5f);
}

TEST(PlayerParams, ImpactDefaultsKeepAllFortySevenDisplayNamesAndValues)
{
    Player player;
    const nlohmann::json fields = NS::Obj::SerializeComponent(player.Params())["fields"];
    const nlohmann::json play = {{"反動の高さ", 1.15f},
                                 {"反動の距離", 0.575f},
                                 {"中心近くの当たりの反動の距離の倍率", 4.5f},
                                 {"中心近くの当たりの反動の高さの倍率", 0.6f},
                                 {"押し飛ばしの距離", 29.0f},
                                 {"押し飛ばしの質量指数", 0.35f},
                                 {"押し飛ばしの高さ", 2.0f},
                                 {"押し飛ばしの上昇重力", 25.0f},
                                 {"下りの速さの倍率", 1.4f},
                                 {"頂点の帯の縦速度", 1.0f},
                                 {"頂点の帯の重力倍率", 0.5f},
                                 {"中心近くの当たりのヒットストップ倍率", 2.0f},
                                 {"ヒットストップの上限秒", 12.0f / 60.0f},
                                 {"破壊を許可", false},
                                 {"貫通時の減速倍率", 0.75f},
                                 {"貫通の止め秒", 4.0f / 60.0f}};
    ExpectPartFields(player.Params(), play);
    const nlohmann::json expected = {{"核の直径の基準", 0.3f},
                                     {"核の直径の威力あたり", 0.2f},
                                     {"核の直径の上限", 0.7f},
                                     {"核の出始めの大きさ", 0.5f},
                                     {"光条の長さの基準", 4.0f},
                                     {"光条の長さの威力あたり", 2.0f},
                                     {"光条の細りきった太さ", 0.3f},
                                     {"輪の半径の基準", 0.5f},
                                     {"輪の半径の威力あたり", 0.6f},
                                     {"輪の出始めの半径", 0.3f},
                                     {"輪をカメラへ起こす割合", 1.0f},
                                     {"火花の数の下限", 10},
                                     {"火花の数の上限", 30},
                                     {"火花の速さの基準", 6.0f},
                                     {"火花の速さの飛ばしの比あたり", 3.0f},
                                     {"大きな外れの火花の数", 16},
                                     {"大きな外れの火花の速さ", 4.0f},
                                     {"火の粉の数の火花あたり", 7.0f},
                                     {"照りの直径の基準", 2.0f},
                                     {"照りの直径の威力あたり", 1.6f},
                                     {"弾かれ線の本数", 8},
                                     {"大きな外れの弾かれ線の本数", 0},
                                     {"弾かれ線の長さの基準", 0.6f},
                                     {"弾かれ線の長さの反動の比あたり", 0.5f},
                                     {"当たりの粉の数の基準", 4},
                                     {"当たりの粉の数を増やす質量の上限", 4.0f},
                                     {"当たりの粉の大きさの基準", 0.8f},
                                     {"当たりの粉の大きさの質量の平方根あたり", 0.3f},
                                     {"当たりの粉の大きさの威力あたりの伸び", 0.5f},
                                     {"着地の粉の半径の基準", 1.2f},
                                     {"着地の粉の半径の落ちる速さあたり", 0.04f}};
    // 当たりの絵の大きさと量は絵を出す ImpactEffects が持ち、遊びの欄と並ばない
    ExpectPartFields(player.ImpactVisuals(), expected);
    ExpectNotInParams(player, expected);
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
    EXPECT_TRUE(NS::Obj::SerializeComponent(player.Resolver())["fields"].empty());
}

TEST(PlayerParams, LiveImpactVisualTuningDrivesShapeAndLandingDust)
{
    Player player;
    EXPECT_EQ(NS::Obj::ApplyJsonFields(player.ImpactVisuals(),
                                       {{"核の直径の基準", 0.4f},
                                        {"核の直径の威力あたり", 0.3f},
                                        {"核の直径の上限", 2.0f},
                                        {"光条の長さの基準", 3.0f},
                                        {"光条の長さの威力あたり", 4.0f},
                                        {"着地の粉の半径の基準", 2.0f},
                                        {"着地の粉の半径の落ちる速さあたり", 0.1f}}),
              0u);
    NS::Game::Level::ImpactRecord impact{};
    impact.tier = NS::Game::Level::HitTier::Center;
    impact.power = 2.0f;
    const NS::Game::Player::ImpactShape shape = player.ImpactVisuals().ShapeFor(impact);
    EXPECT_FLOAT_EQ(shape.coreDiameter, 1.0f);
    EXPECT_FLOAT_EQ(shape.streakLength, 11.0f);
    EXPECT_FLOAT_EQ(player.ImpactVisuals().LandDustRadiusFor(5.0f), 2.5f);
    EXPECT_FLOAT_EQ(player.ImpactVisuals().LandDustRadiusFor(-1.0f), 2.0f);
}

// 段で変わる絵の決まりは段ごとの 1 行から来る。絵を出す側は段を比べずに形の欄を読む
TEST(PlayerParams, ImpactShapeTakesTheLookFromOneRowPerTier)
{
    using NS::Game::Level::HitTier;
    using NS::Game::Player::CoreHoldMotion;
    using NS::Game::Player::ImpactShape;
    using NS::Game::Player::SparkHeading;
    Player player;
    // 中心近くと大きな外れで本数が違えば、どちらの行から来たかが分かる
    ASSERT_EQ(NS::Obj::ApplyJsonFields(
                  player.ImpactVisuals(),
                  {{"弾かれ線の本数", 9}, {"大きな外れの弾かれ線の本数", 4}, {"大きな外れの火花の大きさ", 2.5f}}),
              0u);
    NS::Game::Level::ImpactRecord impact{};
    impact.power = 1.0f;
    impact.hitStopSteps = 8;
    impact.targetPlaced = true;
    impact.tier = HitTier::Center;
    const ImpactShape center = player.ImpactVisuals().ShapeFor(impact);
    impact.tier = HitTier::Wide;
    const ImpactShape wide = player.ImpactVisuals().ShapeFor(impact);

    EXPECT_EQ(center.coreInput, 0u);
    EXPECT_EQ(center.coreHold, CoreHoldMotion::Pulse);
    EXPECT_EQ(center.sparkHeading, SparkHeading::Launch);
    EXPECT_EQ(center.sparkCountInput, 0u);
    EXPECT_EQ(center.recoilCount, 9);
    EXPECT_EQ(center.recoilCountInput, 0u);
    EXPECT_FALSE(center.coreCut);
    EXPECT_FLOAT_EQ(center.sparkScale, 1.0f);

    EXPECT_EQ(wide.coreInput, 2u);
    EXPECT_EQ(wide.coreHold, CoreHoldMotion::Settle);
    EXPECT_EQ(wide.sparkHeading, SparkHeading::Scrape);
    EXPECT_EQ(wide.sparkCountInput, 1u);
    EXPECT_EQ(wide.recoilCount, 4);
    EXPECT_EQ(wide.recoilCountInput, 2u);
    EXPECT_TRUE(wide.coreCut);
    EXPECT_FLOAT_EQ(wide.sparkScale, 2.5f);

    // 番号から作った段の外の値は、行を混ぜずに大きな外れの行で出す。中心近くの層は 1 つも足さない
    impact.tier = static_cast<HitTier>(3);
    const ImpactShape unknown = player.ImpactVisuals().ShapeFor(impact);
    EXPECT_EQ(unknown.coreInput, wide.coreInput);
    EXPECT_EQ(unknown.coreHold, wide.coreHold);
    EXPECT_EQ(unknown.holdLastFrame, wide.holdLastFrame);
    EXPECT_EQ(unknown.sparkHeading, wide.sparkHeading);
    EXPECT_EQ(unknown.sparkCountInput, wide.sparkCountInput);
    EXPECT_EQ(unknown.sparkCount, wide.sparkCount);
    EXPECT_EQ(unknown.recoilCount, wide.recoilCount);
    EXPECT_EQ(unknown.recoilCountInput, wide.recoilCountInput);
    EXPECT_FLOAT_EQ(unknown.streakLength, 0.0f);
    EXPECT_FLOAT_EQ(unknown.ringRadius, 0.0f);
    EXPECT_EQ(unknown.emberCount, 0);
    EXPECT_FLOAT_EQ(unknown.glowDiameter, 0.0f);
}

TEST(PlayerParams, LiveImpactTuningDrivesReboundAndLaunchRecord)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SetObjectPosition(entry, NS::Vector3{0.0f, 1.0f, 0.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 2);
    // 自機の玉の中心 (根 1 m − 半分の高さ 0.5 m) と同じ高さ。赤の真ん中に当たり、威力の倍率は 1
    NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 1.0f});
    // 位置は部品の件 Transform に入っているので、件ごと置き換えずに足す
    NS::Obj::ObjectJsonParts(rock)["Params"] = {{"質量", 1.0f}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    Player* player = NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(player->Params(),
                                       {{"押し飛ばしの距離", 8.0f},
                                        {"押し飛ばしの高さ", 3.0f},
                                        {"反動の高さ", 2.0f},
                                        {"中心近くの当たりの反動の高さの倍率", 1.0f}}),
              0u);
    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Update(false);
    ASSERT_TRUE(player->Resolver().DidRebound());
    const NS::Game::Level::ImpactRecord& impact = player->Resolver().LastImpact();
    EXPECT_FLOAT_EQ(impact.launchDistance, 8.0f);
    EXPECT_FLOAT_EQ(impact.launchApexHeight, 3.0f);
    EXPECT_FLOAT_EQ(impact.reboundApexHeight, 2.0f);
}

TEST(PlayerParams, IndicatorDefaultsKeepAllThirtyNineFields)
{
    Player player;
    const NS::Game::Level::TargetMarkerDesc marker{};
    const NS::Game::Level::SlamArrowDesc arrow{};
    const nlohmann::json markerFields = {
        {"印の色", {marker.color.x, marker.color.y, marker.color.z}},
        {"印の太さ", marker.lineThickness},
        {"印の腕の割合", marker.armRatio},
        {"枠と輪郭の間", marker.frameGap},
        {"枠の一辺の下限", marker.frameMinSide},
        {"枠の不透明度", marker.frameAlpha},
        {"枠が出る時の倍率", marker.appearScale},
        {"枠が出る時の一辺の上限", marker.appearMaxSide},
        {"枠が縮むフレーム数", marker.appearFrames},
        {"枠が縮む進みの曲線",
         {{"curve", {{0.0f, 0.0f}, {0.25f, 0.12f}, {0.5f, 0.38f}, {0.75f, 0.70f}, {1.0f, 1.0f}}}}},
        {"枠が出る時の色", {marker.appearColor.x, marker.appearColor.y, marker.appearColor.z}},
        {"枠が出る時の不透明度", marker.appearAlpha},
        {"外れた時の枠の倍率", marker.lostScale},
        {"外れた時の枠のフレーム数", marker.lostFrames},
        {"枠の縁の色", {marker.outlineColor.x, marker.outlineColor.y, marker.outlineColor.z}},
        {"枠の縁の不透明度", marker.outlineAlpha}};
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
        {"溜めの前半の色", {arrow.earlyColor.x, arrow.earlyColor.y, arrow.earlyColor.z}},
        {"溜めの後半の色", {arrow.lateColor.x, arrow.lateColor.y, arrow.lateColor.z}},
        {"溜めきりの色", {arrow.fullColor.x, arrow.fullColor.y, arrow.fullColor.z}},
        {"色の付いていない部分の色", {arrow.plainColor.x, arrow.plainColor.y, arrow.plainColor.z}},
        {"矢印の暗い縁の色", {arrow.darkColor.x, arrow.darkColor.y, arrow.darkColor.z}},
        {"矢印の暗い縁の不透明度", arrow.darkAlpha},
        {"帯の明るい縁の不透明度", arrow.bandEdgeAlpha},
        {"帯の塗りの不透明度", arrow.bandFillAlpha},
        {"矢じりの明るい縁の不透明度", arrow.headEdgeAlpha},
        {"矢じりの塗りの不透明度", arrow.headFillAlpha},
        {"色の無い帯の明るい縁の不透明度", arrow.plainBandEdgeAlpha},
        {"色の無い帯の塗りの不透明度", arrow.plainBandFillAlpha},
        {"隠れた矢じりの不透明度", arrow.occludedHeadAlpha},
        {"矢じりを見せる最小の角度", arrow.headMinViewDegrees}};
    // 枠と矢印の見た目は描く部品が持ち、遊びの欄と並ばない
    ExpectPartFields(player.TargetIndicator(), markerFields);
    ExpectPartFields(player.SlamIndicator(), arrowFields);
    ExpectNotInParams(player, markerFields);
    ExpectNotInParams(player, arrowFields);
}

TEST(PlayerParams, LiveIndicatorTuningReachesTheShownShapes)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 2);
    NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.0f, 4.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    Player* player = NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    const NS::Vector3 eye{0.0f, 0.0f, -6.0f};
    const NS::Vector3 lookAt{0.0f, 0.0f, 4.0f};
    ASSERT_NE(PlaceViewCamera(scene, eye, lookAt), nullptr);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(player->Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(player->TargetIndicator(), {{"印の太さ", 7.0f}}), 0u);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(player->SlamIndicator(),
                                       {{"溜めの前半の色", {0.2f, 0.3f, 0.4f}}, {"矢印が伸びるフレーム数", 1}}),
              0u);
    // しきい値のフレームまで押して溜めに入れる
    const int threshold = static_cast<int>(std::lround(0.2f / NS::OS::FrameTimer::FixedDelta()));
    for (int frame = 0; frame < threshold; ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    NS::Game::Level::LockOnFrameShape frame{};
    const NS::Matrix view = NS::Matrix::CreateLookAt(eye, lookAt, NS::Vector3::UnitY);
    const NS::Matrix projection =
        NS::Matrix::CreatePerspectiveFieldOfView(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    ASSERT_TRUE(player->TargetIndicator().BuildShownShape(view * projection, NS::Size2D{1280, 720}, frame));
    ASSERT_EQ(frame.corners.size(), 8u);
    EXPECT_FLOAT_EQ(frame.corners.front().height, 7.0f);
    NS::Game::Level::SlamArrowShape arrow{};
    ASSERT_TRUE(player->SlamIndicator().TryGetShownArrow(arrow));
    EXPECT_FLOAT_EQ(arrow.stageColor.x, 0.2f);
    EXPECT_FLOAT_EQ(arrow.stageColor.y, 0.3f);
    EXPECT_FLOAT_EQ(arrow.stageColor.z, 0.4f);
    EXPECT_FLOAT_EQ(arrow.tip, arrow.fullTip);
}

// 欄を別の部品へ移した後も、同梱の種類の既定値と場面の値は読まれる。古い部品の下に残った鍵はどの欄にも
// 照合されず、詰めた値が静かに既定へ戻る
TEST(PlayerParams, ShippedAssetsKeepEveryFieldUnderThePartThatReadsIt)
{
    const auto expectEveryFieldRead = [](const nlohmann::json& entry, std::string_view source) {
        Player player;
        const nlohmann::json& parts = NS::Obj::ObjectJsonParts(entry);
        for (nlohmann::json::const_iterator it = parts.begin(); it != parts.end(); ++it)
        {
            NS::Obj::Component* part = player.Part(it.key());
            ASSERT_NE(part, nullptr) << source << " " << it.key();
            EXPECT_EQ(NS::Obj::ApplyJsonFields(*part, it.value()), 0u) << source << " " << it.key();
        }
    };
    NS::Obj::ArchetypeLibrary::Get().Reload();
    const nlohmann::json* archetype = NS::Obj::ArchetypeLibrary::Get().Find("Player");
    ASSERT_NE(archetype, nullptr);
    expectEveryFieldRead(*archetype, "Player.json");

    for (const std::string_view sceneName : {"new_scene.scene", "course.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(
                NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::Obj::SceneJsonObjects(doc))
        {
            if (NS::Obj::ObjectJsonClass(entry) == "Player")
            {
                expectEveryFieldRead(entry, sceneName);
            }
        }
    }
}
