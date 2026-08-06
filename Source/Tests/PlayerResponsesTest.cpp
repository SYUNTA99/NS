#include "Game/Level/KillZoneComponent.h"
#include "Game/Level/RespawnerComponent.h"
#include "Game/Level/ScreenFadeComponent.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/PlayerInputComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

/// Application 依存のない Scene で、 走行のやり直しと応答 component の挙動を検証する
/// 応答部品はプレイヤーに載るので、 プレイヤーを 1 体置けば揃う
/// 世界の駆動は Scene::OnUpdate で、 dt は FrameTimer::FixedDelta の既定 1/60 が使われる
/// 判定と応答 (respawner / finisher) は同じ LateUpdate 帯の並びで済む

namespace
{
    // 接触クリアの印だけを持つゴールを組む
    SceneNs::ObjectData MakeGoal(float x, float y, float z)
    {
        SceneNs::ObjectData object;
        SceneNs::SetObjectPosition(object, NS::Math::Vector3{x, y, z});
        object.components.push_back(SceneNs::MakeComponentEntry("GoalComponent"));
        return object;
    }

    // プレイヤーと同じ場所に置くトリガの hazard 箱を組む。 トリガなので物理に押し出されない
    SceneNs::ObjectData MakeTriggerHazard()
    {
        SceneNs::ObjectData object;
        nlohmann::json box = SceneNs::MakeComponentEntry("BoxColliderComponent");
        SceneNs::SetField(box, "トリガー", true);
        object.components = nlohmann::json::array({std::move(box), SceneNs::MakeComponentEntry("HazardComponent")});
        return object;
    }

    // 応答 component を載せた GameObject から暗転を引く。 GameObject は無名なので component 検索で見つける
    LevelNs::ScreenFadeComponent* FindFade(SceneNs::Scene& scene)
    {
        LevelNs::ScreenFadeComponent* found = nullptr;
        scene.World().ForEachComponent<LevelNs::ScreenFadeComponent>([&found](LevelNs::ScreenFadeComponent& fade) {
            if (found == nullptr)
                found = &fade;
        });
        return found;
    }
} // namespace

TEST(PlayerResponses, RestartRunPlacesPlayerAtBaseline)
{
    SceneNs::Scene scene;
    SceneNs::SceneData live;
    live.objects.push_back(MakePlayerObject(NS::Math::Vector3{1.0f, 1.0f, 1.0f}, NS::Math::Quaternion{}));
    scene.LoadFromData(std::move(live));
    SceneNs::SceneData baseline;
    baseline.objects.push_back(MakePlayerObject(NS::Math::Vector3{7.0f, 2.0f, -4.0f}, NS::Math::Quaternion{}));
    scene.SetPlayBaselineForTest(std::move(baseline));

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    // やり直しの手順はプレイヤーに載る respawner の持ち物
    player->FindComponent<LevelNs::RespawnerComponent>()->RestartRun();

    EXPECT_NEAR(player->Root().Position().x, 7.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().z, -4.0f, 1e-4f);
}

TEST(PlayerResponses, FallIntoKillZoneRestartsSameTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(LevelNs::MakeKillZoneObject());
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    // 体力を減らしておくと、 全回復が「リスタートが走った」証拠になる
    player->ApplyDamage(5);

    // 奈落へ落とすと LateUpdate 帯の即死判定が立ち、 同じ LateUpdate の respawner がリスタートさせる
    player->Root().SetPosition(NS::Math::Vector3{0.0f, -55.0f, 0.0f});
    scene.OnUpdate();

    EXPECT_FALSE(player->IsDead());
    EXPECT_EQ(player->Health(), 8);
    EXPECT_NEAR(player->Root().Position().x, 0.0f, 1e-3f);
    EXPECT_GT(player->Root().Position().y, -50.0f);
}

TEST(PlayerResponses, HazardDrainsExactlyOncePerTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeTriggerHazard());
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    scene.OnUpdate();

    // 判定が 1 tick に 2 回走ると 6 になる
    EXPECT_EQ(player->Health(), 7);
}

TEST(PlayerResponses, GoalContactStartsClearFadeSameTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // プレイヤー実体と同じ位置にゴールを置くと中心距離 0 で必ず接触する
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    scene.OnUpdate();

    // 接触のフラグが判定で立ち、 同じ LateUpdate の finisher がシーケンスを始める
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    EXPECT_TRUE(fade->IsFading());
}

TEST(PlayerResponses, PausedTickAdvancesNothing)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeTriggerHazard());
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    player->ApplyDamage(5);
    scene.SetSimulationPaused(true);
    scene.OnUpdate();

    // 時間停止中は hazard の中に居てもルール評価は走らない
    EXPECT_EQ(player->Health(), 3);
}

TEST(PlayerResponses, StepFrameAdvancesExactlyOneTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeTriggerHazard());
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);

    // コマ送り 1 回で hazard がちょうど 1 削り、 その後は止まったまま
    scene.StepSimulation();
    scene.OnUpdate();
    EXPECT_EQ(player->Health(), 7);
    scene.OnUpdate();
    EXPECT_EQ(player->Health(), 7);
}

TEST(PlayerResponses, DisabledSimulationSkipsWorld)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeTriggerHazard());
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);

    // 編集モード相当。 世界を回さないので hazard の中でも何も起きない
    scene.SetSimulationEnabled(false);
    scene.OnUpdate();
    EXPECT_EQ(player->Health(), 8);

    // 回し直すと同じ tick からルール評価が戻る
    scene.SetSimulationEnabled(true);
    scene.OnUpdate();
    EXPECT_EQ(player->Health(), 7);
}

TEST(PlayerResponses, ClearFadesOutRestartsAtBlackThenFadesIn)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    // 体力を減らしておくと、 全回復が「全黒でリスタートが走った」証拠になる
    auto* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    player->ApplyDamage(5);
    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // シーケンスの間は世界を止めず入力だけ切る
    auto* input = player->FindComponent<SceneNs::PlayerInputComponent>();
    ASSERT_NE(input, nullptr);
    EXPECT_FALSE(input->IsActiveSelf());

    // 暗転が終わると全黒の裏でリスタートし、 そのまま明転に入る
    int ticks = 0;
    while (player->Health() != 8 && ticks++ < 100)
        scene.OnUpdate();
    ASSERT_LT(ticks, 100);
    EXPECT_TRUE(fade->IsFading());
    EXPECT_NEAR(fade->Alpha(), 1.0f, 1e-4f);

    // 明転を終えると通常プレイへ戻り、 操作が返る
    ticks = 0;
    while (fade->IsFading() && ticks++ < 100)
        scene.OnUpdate();
    ASSERT_LT(ticks, 100);
    EXPECT_NEAR(fade->Alpha(), 0.0f, 1e-6f);
    EXPECT_TRUE(input->IsActiveSelf());
}
