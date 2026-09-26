#include "Game/Level/Goal.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Level/Respawner.h"
#include "Game/Level/ScreenFade.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <cmath>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

//! Application 依存のない Scene で、走行のやり直しと応答 component の挙動を検証する
//! 応答部品はプレイヤーに載るので、プレイヤーを 1 体置けば揃う
//! 更新を回すのは Scene::OnUpdate で、dt は FrameTimer::FixedDelta の既定 1/60 が使われる
//! 判定と応答 (respawner / finisher) は同じ LateUpdate 帯の並びで済む

namespace
{
    // 接触クリアの印だけを持つゴールを組む
    nlohmann::json MakeGoal(float x, float y, float z)
    {
        nlohmann::json object = SceneNs::MakeObjectJson();
        SceneNs::SetObjectPosition(object, NS::Core::Vector3{x, y, z});
        SceneNs::ObjectJsonComponents(object).push_back(SceneNs::MakeComponentEntry("Goal"));
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

    // 押し飛ばせる球の移動と回転の減衰。曲線の間は 0 に切られ、置かれた物へ戻ると配置の値に戻る
    constexpr float k_RockDamping = 3.0f;
    const NS::Core::Vector3 k_RockPosition{4.0f, 5.0f, 0.0f};
    const NS::Core::Quaternion k_RockRotation =
        NS::Core::EulerDegreesToQuaternion(NS::Core::Vector3{0.0f, 90.0f, 0.0f});
    // 距離と高さだけを入れた曲線。重力と頂点の帯は既定の値
    const LevelNs::LaunchArc k_RockArc{
        .direction = NS::Core::Vector3{1.0f, 0.0f, 0.0f}, .distance = 10.0f, .apexHeight = 2.0f};
    // 剛体で飛ばす時の速度 (m/s)
    const NS::Core::Vector3 k_RockVelocity{3.0f, 4.0f, 0.0f};

    // 押し飛ばせる球を組む。同梱の球と同じく、置かれている間はキネマティックで、減衰を持つ
    nlohmann::json MakeRock(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation)
    {
        nlohmann::json object = SceneNs::MakeObjectJson();
        SceneNs::SetObjectPosition(object, position);
        SceneNs::SetObjectRotation(object, rotation);
        SceneNs::ObjectJsonComponents(object).push_back(SceneNs::MakeComponentEntry("SphereCollider"));
        nlohmann::json rigidBody = SceneNs::MakeComponentEntry("RigidBody");
        SceneNs::SetField(rigidBody, "キネマティック", true);
        SceneNs::SetField(rigidBody, "移動の減衰", k_RockDamping);
        SceneNs::SetField(rigidBody, "回転の減衰", k_RockDamping);
        SceneNs::ObjectJsonComponents(object).push_back(rigidBody);
        SceneNs::ObjectJsonComponents(object).push_back(SceneNs::MakeComponentEntry("LaunchedBody"));
        return object;
    }

    // 場面に置いた順に、押し飛ばせる物を集める
    std::vector<LevelNs::LaunchedBody*> FindRocks(SceneNs::Scene& scene)
    {
        std::vector<LevelNs::LaunchedBody*> rocks;
        scene.Objects().ForEachComponent<LevelNs::LaunchedBody>(
            [&rocks](LevelNs::LaunchedBody& body) { rocks.push_back(&body); });
        return rocks;
    }

    // 根が position と rotation にあり、速度も回る速さも無いキネマティックの置かれた物になったかを見る
    void ExpectPlacedAt(const LevelNs::LaunchedBody& body,
                        const NS::Core::Vector3& position,
                        const NS::Core::Quaternion& rotation)
    {
        const NS::Core::Vector3 placed = body.RootTransform().Position();
        EXPECT_NEAR(placed.x, position.x, 1e-4f);
        EXPECT_NEAR(placed.y, position.y, 1e-4f);
        EXPECT_NEAR(placed.z, position.z, 1e-4f);
        // 同じ向きなら内積の大きさが 1
        EXPECT_NEAR(std::fabs(body.RootTransform().Rotation().Dot(rotation)), 1.0f, 1e-4f);
        EXPECT_EQ(body.Phase(), LevelNs::LaunchPhase::Resting);

        const SceneNs::RigidBody* rigidBody = body.Owner()->FindComponent<SceneNs::RigidBody>();
        ASSERT_NE(rigidBody, nullptr);
        EXPECT_TRUE(rigidBody->IsKinematic());
        EXPECT_NEAR(rigidBody->Velocity().Length(), 0.0f, 1e-4f);
        EXPECT_NEAR(rigidBody->AngularVelocity().Length(), 0.0f, 1e-4f);
    }
} // namespace

TEST(PlayerResponses, RestartRunPlacesPlayerAtBaseline)
{
    SceneNs::Scene scene;
    nlohmann::json live = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(live).push_back(
        MakePlayerObject(NS::Core::Vector3{1.0f, 1.0f, 1.0f}, NS::Core::Quaternion{}));
    scene.LoadJson(std::move(live));
    nlohmann::json baseline = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(baseline).push_back(
        MakePlayerObject(NS::Core::Vector3{7.0f, 2.0f, -4.0f}, NS::Core::Quaternion{}));
    scene.SetPlayBaselineForTest(std::move(baseline));

    Player* player = FindPlayer(scene.Objects());
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
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(LevelNs::MakeKillZoneObject());
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    Player* player = FindPlayer(scene.Objects());
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
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // プレイヤー実体と同じ位置にゴールを置くと中心距離 0 で必ず接触する
    SceneNs::SceneJsonObjects(data).push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    scene.OnUpdate();

    // 接触のフラグが判定で立ち、同じ LateUpdate の finisher がシーケンスを始める
    LevelNs::ScreenFade* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    EXPECT_TRUE(fade->IsFading());
}

TEST(PlayerResponses, PausedTickAdvancesNothing)
{
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    scene.SetSimulationPaused(true);
    scene.OnUpdate();

    // 時間停止中はゴールに重なっていてもルール評価は走らない
    EXPECT_FALSE(GoalReached(scene));
}

TEST(PlayerResponses, StepFrameAdvancesExactlyOneTick)
{
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    // コマ送り 1 回でゴールの暗転が始まる
    scene.StepSimulation();
    scene.OnUpdate();
    LevelNs::ScreenFade* fade = FindFade(scene);
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
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadJson(std::move(data));
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
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    // 体力を減らしておくと、全回復が「全黒でリスタートが走った」証拠になる
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->ApplyDamage(5);
    scene.OnUpdate();
    LevelNs::ScreenFade* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // シーケンスの間も Scene の更新は止めず入力だけ切る
    SceneNs::PlayerInput* input = player->FindComponent<SceneNs::PlayerInput>();
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

// 曲線で飛んでいる物は、やり直しで凍結の位置と回転へ置かれた物として戻り、曲線の間に切った欄も配置の値へ戻る
TEST(PlayerResponses, RestartRunReturnsArcFlyingBodyToBaseline)
{
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(MakeRock(k_RockPosition, k_RockRotation));
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    const std::vector<LevelNs::LaunchedBody*> rocks = FindRocks(scene);
    ASSERT_EQ(rocks.size(), 1u);
    LevelNs::LaunchedBody& rock = *rocks[0];
    rock.Launch(k_RockArc);
    for (int i = 0; i < 3; ++i)
    {
        scene.OnUpdate();
    }
    SceneNs::RigidBody* rigidBody = rock.Owner()->FindComponent<SceneNs::RigidBody>();
    ASSERT_NE(rigidBody, nullptr);
    ASSERT_EQ(rock.Phase(), LevelNs::LaunchPhase::Arc);
    ASSERT_FALSE(rigidBody->UsesGravity());

    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->FindComponent<LevelNs::Respawner>()->RestartRun();

    ExpectPlacedAt(rock, k_RockPosition, k_RockRotation);
    EXPECT_TRUE(rigidBody->UsesGravity());
    EXPECT_FLOAT_EQ(rigidBody->LinearDamping(), k_RockDamping);
    EXPECT_FLOAT_EQ(rigidBody->AngularDamping(), k_RockDamping);

    // body も同じ所へ置かれているので、次のフレームの物理でも動かない
    scene.OnUpdate();
    ExpectPlacedAt(rock, k_RockPosition, k_RockRotation);
}

// 剛体で転がっている物と、止まった後に位置だけ動いている物も、やり直しで凍結の位置へ戻る
TEST(PlayerResponses, RestartRunReturnsRollingAndRestedBodiesToBaseline)
{
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    const NS::Core::Vector3 restedPosition{-4.0f, 5.0f, 0.0f};
    SceneNs::SceneJsonObjects(data).push_back(MakeRock(k_RockPosition, k_RockRotation));
    SceneNs::SceneJsonObjects(data).push_back(MakeRock(restedPosition, k_RockRotation));
    scene.LoadJson(std::move(data));
    (void)scene.BeginPlayBaseline();

    const std::vector<LevelNs::LaunchedBody*> rocks = FindRocks(scene);
    ASSERT_EQ(rocks.size(), 2u);
    LevelNs::LaunchedBody& rolling = *rocks[0];
    LevelNs::LaunchedBody& rested = *rocks[1];
    rolling.LaunchRigid(k_RockVelocity);
    // 止まった物は置かれた物の段階のまま、止まった所だけが置いた位置と違う
    SceneNs::RigidBody* restedBody = rested.Owner()->FindComponent<SceneNs::RigidBody>();
    ASSERT_NE(restedBody, nullptr);
    restedBody->Teleport(NS::Core::Vector3{-9.0f, 0.5f, 6.0f}, NS::Core::Quaternion{});
    for (int i = 0; i < 3; ++i)
    {
        scene.OnUpdate();
    }
    ASSERT_EQ(rolling.Phase(), LevelNs::LaunchPhase::Rigid);
    ASSERT_EQ(rested.Phase(), LevelNs::LaunchPhase::Resting);

    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->FindComponent<LevelNs::Respawner>()->RestartRun();

    ExpectPlacedAt(rolling, k_RockPosition, k_RockRotation);
    ExpectPlacedAt(rested, restedPosition, k_RockRotation);
}

// 凍結に無い配置物は、やり直しでも飛んでいるまま動かさない
TEST(PlayerResponses, RestartRunLeavesBodiesMissingFromBaseline)
{
    SceneNs::Scene scene;
    nlohmann::json live = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(live).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(live).push_back(MakeRock(k_RockPosition, k_RockRotation));
    scene.LoadJson(std::move(live));
    nlohmann::json baseline = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(baseline).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    scene.SetPlayBaselineForTest(std::move(baseline));

    const std::vector<LevelNs::LaunchedBody*> rocks = FindRocks(scene);
    ASSERT_EQ(rocks.size(), 1u);
    LevelNs::LaunchedBody& rock = *rocks[0];
    rock.LaunchRigid(k_RockVelocity);
    for (int i = 0; i < 3; ++i)
    {
        scene.OnUpdate();
    }
    ASSERT_EQ(rock.Phase(), LevelNs::LaunchPhase::Rigid);
    const NS::Core::Vector3 flying = rock.RootTransform().Position();

    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->FindComponent<LevelNs::Respawner>()->RestartRun();

    EXPECT_EQ(rock.Phase(), LevelNs::LaunchPhase::Rigid);
    const NS::Core::Vector3 after = rock.RootTransform().Position();
    EXPECT_FLOAT_EQ(after.x, flying.x);
    EXPECT_FLOAT_EQ(after.y, flying.y);
    EXPECT_FLOAT_EQ(after.z, flying.z);
}
