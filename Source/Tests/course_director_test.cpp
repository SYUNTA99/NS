#include "Editor/EditorObjects.h"
#include "Game/Game.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Body.h"
#include "NSlib/CoreObject/SubObjects/PlayerInput.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"
#include "NSlib/Windows/Mouse.h"

#include <gtest/gtest.h>

// 落下死とゴールがメッセージで伝わり、コースの進行役が流れを進めることを縛る

namespace
{
    // 本物のマウス左を押したままにする。試しの終わりにマウスの状態を空へ戻す
    struct MouseLeftPress
    {
        MouseLeftPress() noexcept { NS::OS::Input::Get().Mouse().OnButtonDown(NS::OS::MouseButton::Left); }
        ~MouseLeftPress() noexcept { NS::OS::Input::Get().Mouse().ClearState(); }
        MouseLeftPress(const MouseLeftPress&) = delete;
        MouseLeftPress& operator=(const MouseLeftPress&) = delete;
    };

    // 本物のキーを押したままにする。試しの終わりにキーボードの状態を空へ戻す
    struct KeyPress
    {
        explicit KeyPress(NS::OS::Key key) noexcept { NS::OS::Input::Get().Keyboard().OnKeyDown(key); }
        ~KeyPress() noexcept { NS::OS::Input::Get().Keyboard().ClearState(); }
        KeyPress(const KeyPress&) = delete;
        KeyPress& operator=(const KeyPress&) = delete;
    };

    // 本番の入力の段と PlayerActor の段を 1 歩ずつ回す
    void StepPlayer(PlayerActor& player)
    {
        player.ReadInput();
        player.Update();
    }

    // プレイヤーと落下死の範囲を敷いたシーン文書
    nlohmann::json CourseWithPlayer()
    {
        nlohmann::json doc = NS::MakeSceneJson();
        (void)NS::Editor::EnsurePlayerObject(doc);
        (void)NS::Editor::EnsureDeathZoneObject(doc);
        return doc;
    }

    NS::Vector3 SpawnOf(const nlohmann::json& doc)
    {
        return NS::ObjectPosition(NS::SceneJsonObjects(doc)[NS::Editor::FindPlayerObjectIndex(doc)]);
    }

    // ゴールを自機の出現位置に重ねたシーン文書
    nlohmann::json CourseWithPlayerOnGoal()
    {
        nlohmann::json doc = CourseWithPlayer();
        nlohmann::json goal = NS::MakePrototypeJson<GL::Level::GoalActor>();
        NS::SetObjectPosition(goal, SpawnOf(doc));
        NS::SceneJsonObjects(doc).push_back(std::move(goal));
        NS::EnsureUniqueObjectIds(doc);
        return doc;
    }
} // namespace

TEST(CourseDirector, PlayerCreatesDirectorAfterPlacement)
{
    NS::Scene scene;
    scene.ResetFromJson(CourseWithPlayer());
    EXPECT_NE(NS::FindSceneSystem<GL::Level::CourseDirector>(&scene), nullptr);

    // 組み直すと捨てられ、プレイヤーの居ないシーンでは作られない
    scene.ResetFromJson(NS::MakeSceneJson());
    EXPECT_EQ(NS::FindSceneSystem<GL::Level::CourseDirector>(&scene), nullptr);
}

TEST(CourseDirector, ClearSequenceUsesTheSceneFadeAndUnlocksInput)
{
    NS::Scene scene;
    scene.ResetFromJson(CourseWithPlayer());
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);
    director->StartCourse();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    const NS::PlayerInput* input = NS::Cast<NS::PlayerInput>(player->FindSubObj("Input"));
    ASSERT_NE(input, nullptr);
    const float duration = NS::OS::FrameTimer::FixedDelta() * 2.0f;
    director->NotifyGoal(duration, duration);

    scene.OnUpdate();
    ASSERT_TRUE(director->IsClearing());
    EXPECT_TRUE(input->IsLocked());
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.OnUpdate();
    EXPECT_TRUE(scene.Fade().IsBlack());
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.0f);
    scene.OnUpdate();
    EXPECT_FALSE(director->IsClearing());
    EXPECT_FALSE(input->IsLocked());
}

TEST(CourseDirector, RemovingAnIdleDirectorKeepsAnIndependentSceneFade)
{
    NS::Scene scene;
    (void)NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene);
    scene.Fade().BeginOut(0.0f);
    scene.GetSceneSystemHolder()->Clear();
    EXPECT_TRUE(scene.Fade().IsBlack());
}

TEST(CourseDirector, DeathZoneEndsPlayerAndDirectorRestartsCourse)
{
    const nlohmann::json doc = CourseWithPlayer();
    NS::Scene scene;
    scene.ResetFromJson(doc);
    (void)scene.BeginPlayBaseline();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);

    // 落下死の範囲 (上面 y=-50) の中へ落とす
    player->Root().SetPosition(NS::Vector3{0.0f, -52.0f, 0.0f});
    scene.HitSensors().OnTick();
    EXPECT_TRUE(player->IsDead());

    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);
    director->OnTick();
    // 出現位置へ戻り、命も満タンへ戻る
    EXPECT_FALSE(player->IsDead());
    EXPECT_FLOAT_EQ(player->Root().Position().y, SpawnOf(doc).y);
}

// 置物の出し直しのボタンを押すと、次の進行役の段で岩だけが置いた所へ戻り、自機はその場に残る
TEST(CourseDirector, RespawnButtonReturnsObjectsButKeepsPlayer)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonClass(rock, "PropActor");
    NS::SetObjectPosition(rock, NS::Vector3{5.0f, 1.0f, 0.0f});
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    NS::EnsureUniqueObjectIds(doc);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    (void)scene.BeginPlayBaseline();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);
    NS::SceneActor* placed = nullptr;
    for (NS::SceneActor* actor : scene.Objects())
    {
        if (std::string_view{actor->ClassName()} == "PropActor")
        {
            placed = actor;
        }
    }
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    const NS::Vector3 spawn = SpawnOf(doc);
    const NS::Vector3 movedTo{spawn.x + 3.0f, spawn.y, spawn.z + 3.0f};
    player->Root().SetPosition(movedTo);

    {
        const KeyPress press{NS::OS::Key::R};
        StepPlayer(*player);
    }
    director->OnTick();

    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
    EXPECT_FLOAT_EQ(player->Root().Position().x, movedTo.x);
    EXPECT_FLOAT_EQ(player->Root().Position().z, movedTo.z);
}

TEST(CourseDirector, GoalStartsClearSequenceAndLocksInput)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json goal = NS::MakePrototypeJson<GL::Level::GoalActor>();
    NS::SetObjectPosition(goal, SpawnOf(doc));
    NS::SceneJsonObjects(doc).push_back(std::move(goal));
    NS::EnsureUniqueObjectIds(doc);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    (void)scene.BeginPlayBaseline();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);

    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
    EXPECT_TRUE(scene.Fade().IsFading());
    const NS::PlayerInput* input = NS::Cast<NS::PlayerInput>(player->FindSubObj("Input"));
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(input->IsLocked());

    // 流れの最中に届くゴールの知らせは捨てる
    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
}

// 体当たりのボタンを止めを見ずに読むと、暗転の間も押したままの自機が溜め続ける
TEST(CourseDirector, ClearLockStopsTheHeldSlamButton)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    NS::Scene scene;
    scene.ResetFromJson(doc);
    (void)scene.BeginPlayBaseline();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);

    MouseLeftPress press;
    for (int i = 0; i < 60 && !player->ChargeJudge().IsCharging(); ++i)
    {
        StepPlayer(*player);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());

    // 溜めている間に落ちた分を戻してゴールに重ねる
    player->Root().SetPosition(SpawnOf(doc));
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());

    for (int i = 0; i < 10; ++i)
    {
        StepPlayer(*player);
        EXPECT_FALSE(player->ChargeJudge().IsCharging()) << "歩 " << i;
        EXPECT_FALSE(player->IsBodySlamming()) << "歩 " << i;
    }
}

// 止めた瞬間の向きを持ち続けると、暗転の間もゴールで倒していた向きへ歩き続ける
TEST(CourseDirector, ClearLockNeutralizesTheMoveInput)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    NS::Scene scene;
    scene.ResetFromJson(doc);
    (void)scene.BeginPlayBaseline();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        player->Update();
    }
    const float lockedSpeed = player->Body().LateralVelocity().Length();
    ASSERT_GT(lockedSpeed, 0.0f);

    player->Root().SetPosition(SpawnOf(doc));
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());

    for (int i = 0; i < 10; ++i)
    {
        StepPlayer(*player);
    }
    EXPECT_FLOAT_EQ(player->DesiredDirection().Length(), 0.0f);
    EXPECT_FLOAT_EQ(player->DesiredSpeedScale(), 0.0f);
    // 床の無い空中では手を放しても横は減らないので、加速し続けないことを見る
    EXPECT_LE(player->Body().LateralVelocity().Length(), lockedSpeed);
}

// 押しが偽になった歩を放したと読むと、溜めの最中の止めで溜めた突進が出る
TEST(CourseDirector, InputLockDropsTheChargeInsteadOfReleasingIt)
{
    NS::Scene scene;
    scene.ResetFromJson(CourseWithPlayer());
    (void)scene.BeginPlayBaseline();
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);

    for (int i = 0; i < 60 && !player->ChargeJudge().IsCharging(); ++i)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    const NS::uint32 impacts = player->Resolver().Answer().lastImpact.sequence;

    EXPECT_TRUE(GL::Level::SendMsgInputLock(*player, true));
    EXPECT_FALSE(player->ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player->StanceHeight(), 1.0f);

    player->Update(false);
    EXPECT_FALSE(player->IsBodySlamming());
    EXPECT_EQ(player->Resolver().Answer().lastImpact.sequence, impacts);
}

TEST(CourseDirector, RestartReturnsObjectsToBaseline)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonClass(rock, "PropActor");
    NS::SetObjectPosition(rock, NS::Vector3{5.0f, 1.0f, 0.0f});
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    NS::EnsureUniqueObjectIds(doc);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    (void)scene.BeginPlayBaseline();
    NS::SceneActor* placed = nullptr;
    for (NS::SceneActor* actor : scene.Objects())
    {
        if (std::string_view{actor->ClassName()} == "PropActor")
        {
            placed = actor;
        }
    }
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});

    NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene)->RestartCourse();
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
}

TEST(CourseDirector, StartCourseFreezesThePlacedSceneAndStartsEveryoneFromIt)
{
    NS::Scene scene;
    scene.ResetFromJson(CourseWithPlayer());
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 placedAt{5.0f, 5.0f, 5.0f};
    player->Root().SetPosition(placedAt);
    player->Body().SetVelocity(NS::Vector3{2.0f, 4.0f, 3.0f});

    NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene)->StartCourse();

    // 凍結は呼んだ時の配置を写し、自機はそこからやり直しと同じ姿で始まる
    const nlohmann::json& baseline = scene.PlayBaseline();
    const std::size_t index = NS::FindObjectIndexById(baseline, player->Id());
    ASSERT_NE(index, NS::k_NoObjectIndex);
    const NS::Vector3 frozen = NS::ObjectPosition(NS::SceneJsonObjects(baseline)[index]);
    EXPECT_FLOAT_EQ(frozen.x, placedAt.x);
    EXPECT_FLOAT_EQ(frozen.y, placedAt.y);
    EXPECT_FLOAT_EQ(frozen.z, placedAt.z);
    EXPECT_FLOAT_EQ(player->Body().Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().z, 0.0f);
    EXPECT_FLOAT_EQ(player->Root().Position().x, placedAt.x);
    EXPECT_FLOAT_EQ(player->Root().Position().y, placedAt.y);
    EXPECT_FLOAT_EQ(player->Root().Position().z, placedAt.z);
}

TEST(CourseDirector, GameLoadSceneFreezesTheNewlyLoadedScene)
{
    // ServeLayer の台本の差し替えと同じく、立ち上がった後に別のシーンを読み直す
    ::Game game{"Assets/Scenes/new_scene.scene"};
    ASSERT_TRUE(game.LoadScene("Assets/Scenes/new_scene.scene"));
    ASSERT_TRUE(game.LoadScene("Assets/Scenes/course.scene"));
    NS::Scene* scene = game.CurrentScene();
    ASSERT_NE(scene, nullptr);

    // やり直しの戻り先が読み直したシーンの配置になっている
    std::size_t placed = 0;
    for (const NS::SceneActor* actor : scene->Objects())
    {
        if (!actor->IsTransient())
        {
            ++placed;
        }
    }
    ASSERT_GT(placed, 0u);
    EXPECT_EQ(NS::SceneJsonObjects(scene->PlayBaseline()).size(), placed);
    const PlayerActor* player = FindPlayer(scene->Objects());
    ASSERT_NE(player, nullptr);
    EXPECT_NE(NS::FindObjectIndexById(scene->PlayBaseline(), player->Id()), NS::k_NoObjectIndex);
}
