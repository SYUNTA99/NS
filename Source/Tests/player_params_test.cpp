#include "Editor/EditorActors.h"
#include "Game/Level/Health.h"
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
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "Tests/TestSceneSetup.h"
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
    AddActorJson(doc, "PlayerActor", 1);
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
                                     {"振り向きの速さ", 970.0f}};
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
    player.Input().SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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
    const nlohmann::json appearance = {{"立ち姿のメッシュ", "capsule"}, {"玉のメッシュ", "sphere"}};
    ExpectClassObjFields(&player.Appearance(), appearance);
    ExpectNotInParams(&player, appearance);
}

TEST(PlayerParams, ImpactDefaultsKeepGameOwnedDisplayNamesAndValues)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const nlohmann::json fields = NS::SerializeClassObjFields(&player.Params());
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
                                "中心近くの当たりの白のフレーム数",
                                "放つ角度の上限",
                                "反動の上りの重力倍率",
                                "反動中の空中の加速度",
                                "反動の高さ",
                                "反動の距離",
                                "外れのこすって止まるまでのフレーム数",
                                "外れのこすって止まる減り方",
                                "溜めて当てた反動の回転数"})
    {
        EXPECT_FALSE(fields.contains(removed)) << removed;
    }
    // 外れの回り方と着地の潰れは、消した前の反動だけの見た目。欄が戻ると、始める経路の無い値が保存に載る
    const nlohmann::json looks = NS::SerializeClassObjFields(&player.Appearance());
    for (const char* removed : {"外れの縁でのねじれの回転数",
                                "外れで当たる前の回転を引き継ぐ割合",
                                "外れの回転を寄せるフレーム数",
                                "外れの軸のぶれの角度",
                                "外れの軸のぶれの速さ",
                                "着地の潰れ",
                                "着地の潰れを戻すフレーム数"})
    {
        EXPECT_FALSE(looks.contains(removed)) << removed;
    }
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
