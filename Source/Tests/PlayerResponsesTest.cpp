#include "Game/Level/Goal.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/Respawner.h"
#include "Game/Level/ScreenFade.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

//! Application 依存のない Scene で、走行のやり直しと応答 component の挙動を検証する
//! 応答部品はプレイヤーに載るので、プレイヤーを 1 体置けば揃う
//! 更新を回すのは Scene::OnUpdate で、dt は FrameTimer::FixedDelta の既定 1/60 が使われる
//! 判定と応答 (respawner / finisher) は同じ LateUpdate 帯の並びで済む

namespace
{
    // 接触クリアの印だけを持つゴールを組む
    SceneNs::ObjectData MakeGoal(float x, float y, float z)
    {
        SceneNs::ObjectData object;
        SceneNs::SetObjectPosition(object, NS::Core::Vector3{x, y, z});
        object.components.push_back(SceneNs::MakeComponentEntry("Goal"));
        return object;
    }

    // ゴールの接触判定は LateUpdate 帯で走る。印が立ったかで、その tick に帯の更新が回ったかを見る
    bool GoalReached(SceneNs::Scene& scene)
    {
        bool reached = false;
        scene.Objects().ForEachComponent<LevelNs::Goal>([&reached](LevelNs::Goal& goal) {
            if (goal.Reached())
                reached = true;
        });
        return reached;
    }

    // 応答 component を載せた GameObject から暗転を引く。GameObject は無名なので component 検索で見つける
    LevelNs::ScreenFade* FindFade(SceneNs::Scene& scene)
    {
        LevelNs::ScreenFade* found = nullptr;
        scene.Objects().ForEachComponent<LevelNs::ScreenFade>([&found](LevelNs::ScreenFade& fade) {
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
    live.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.0f, 1.0f, 1.0f}, NS::Core::Quaternion{}));
    scene.LoadFromData(std::move(live));
    SceneNs::SceneData baseline;
    baseline.objects.push_back(MakePlayerObject(NS::Core::Vector3{7.0f, 2.0f, -4.0f}, NS::Core::Quaternion{}));
    scene.SetPlayBaselineForTest(std::move(baseline));

    auto* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    // やり直しの手順はプレイヤーに載る respawner の持ち物
    player->FindComponent<LevelNs::Respawner>()->RestartRun();

    EXPECT_NEAR(player->Root().Position().x, 7.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().z, -4.0f, 1e-4f);
}

TEST(PlayerResponses, FallIntoKillZoneRestartsSameTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(LevelNs::MakeKillZoneObject());
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    auto* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    // 体力を減らしておくと、全回復が「リスタートが走った」証拠になる
    player->ApplyDamage(5);

    // 奈落へ落とすと LateUpdate 帯の即死判定が立ち、同じ LateUpdate の respawner がリスタートさせる
    player->Root().SetPosition(NS::Core::Vector3{0.0f, -55.0f, 0.0f});
    scene.OnUpdate();

    EXPECT_FALSE(player->IsDead());
    EXPECT_EQ(player->Health(), 8);
    EXPECT_NEAR(player->Root().Position().x, 0.0f, 1e-3f);
    EXPECT_GT(player->Root().Position().y, -50.0f);
}

TEST(PlayerResponses, GoalContactStartsClearFadeSameTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // プレイヤー実体と同じ位置にゴールを置くと中心距離 0 で必ず接触する
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    scene.OnUpdate();

    // 接触のフラグが判定で立ち、同じ LateUpdate の finisher がシーケンスを始める
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    EXPECT_TRUE(fade->IsFading());
}

TEST(PlayerResponses, PausedTickAdvancesNothing)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    scene.SetSimulationPaused(true);
    scene.OnUpdate();

    // 時間停止中はゴールに重なっていてもルール評価は走らない
    EXPECT_FALSE(GoalReached(scene));
}

TEST(PlayerResponses, StepFrameAdvancesExactlyOneTick)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    // コマ送り 1 回でゴールの暗転が始まる
    scene.StepSimulation();
    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());
    const float afterFirstStep = fade->Alpha();

    // コマ送りしない tick は止まったまま
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(fade->Alpha(), afterFirstStep);

    // 次のコマ送りでだけ暗転が進む
    scene.StepSimulation();
    scene.OnUpdate();
    EXPECT_GT(fade->Alpha(), afterFirstStep);
}

TEST(PlayerResponses, DisabledSimulationSkipsObjectUpdates)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    // 編集モード相当。component の更新が走らないのでゴールに重なっていても何も起きない
    scene.SetSimulationEnabled(false);
    scene.OnUpdate();
    EXPECT_FALSE(GoalReached(scene));

    // 回し直すと同じ tick からルール評価が戻る
    scene.SetSimulationEnabled(true);
    scene.OnUpdate();
    EXPECT_TRUE(GoalReached(scene));
}

TEST(PlayerResponses, ClearFadesOutRestartsAtBlackThenFadesIn)
{
    SceneNs::Scene scene;
    SceneNs::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    (void)scene.BeginPlayBaseline();

    // 体力を減らしておくと、全回復が「全黒でリスタートが走った」証拠になる
    auto* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->ApplyDamage(5);
    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // シーケンスの間も Scene の更新は止めず入力だけ切る
    auto* input = player->FindComponent<SceneNs::PlayerInput>();
    ASSERT_NE(input, nullptr);
    EXPECT_FALSE(input->IsActiveSelf());

    // 暗転が終わると全黒の裏でリスタートし、そのまま明転に入る
    int ticks = 0;
    while (player->Health() != 8 && ticks++ < 100)
        scene.OnUpdate();
    ASSERT_LT(ticks, 100);
    EXPECT_TRUE(fade->IsFading());
    EXPECT_NEAR(fade->Alpha(), 1.0f, 1e-4f);

    // 明転を終えると通常プレイへ戻り、操作が返る
    ticks = 0;
    while (fade->IsFading() && ticks++ < 100)
        scene.OnUpdate();
    ASSERT_LT(ticks, 100);
    EXPECT_NEAR(fade->Alpha(), 0.0f, 1e-6f);
    EXPECT_TRUE(input->IsActiveSelf());
}
