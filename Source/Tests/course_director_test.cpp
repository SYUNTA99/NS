#include "Editor/EditorActors.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"

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

    // 本番と同じく、機器を読む PlayerActor の段を 1 歩回す
    void StepPlayer(PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "StepPlayer: 渡したアクターか部品が無い");
        player->Update();
    }

    // プレイヤーと落下死の範囲を敷いたシーン文書
    nlohmann::json CourseWithPlayer()
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::Editor::MakePrototypeJson<PlayerActor>();
        NS::Editor::SetActorJsonPosition(player, NS::Vector3{0.0f, 2.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(player));
        nlohmann::json zone = NS::Editor::MakePrototypeJson<GL::Level::DeathZoneActor>();
        NS::Editor::SetActorJsonPosition(zone, NS::Vector3{0.0f, -55.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(zone));
        NS::EnsureUniqueActorIds(doc);
        return doc;
    }

    NS::Vector3 SpawnOf(const nlohmann::json& doc)
    {
        return NS::Editor::ActorJsonPosition(NS::SceneJsonActors(doc)[NS::Editor::FindPlayerActorIndex(doc)]);
    }

    // ゴールを自機の出現位置に重ねたシーン文書
    nlohmann::json CourseWithPlayerOnGoal()
    {
        nlohmann::json doc = CourseWithPlayer();
        nlohmann::json goal = NS::Editor::MakePrototypeJson<GL::Level::GoalActor>();
        NS::Editor::SetActorJsonPosition(goal, SpawnOf(doc));
        NS::SceneJsonActors(doc).push_back(std::move(goal));
        NS::EnsureUniqueActorIds(doc);
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
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    const NS::PlayerInput* input = &player->Input();
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
    GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);
    director->StartCourse();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);

    // 落下死の範囲 (上面 y=-50) の中へ落とす
    player->Root().SetPosition(NS::Vector3{0.0f, -52.0f, 0.0f});
    scene.HitSensors().OnTick();
    EXPECT_TRUE(player->IsDead());

    director->OnTick();
    // 出現位置へ戻り、命も満タンへ戻る
    EXPECT_FALSE(player->IsDead());
    EXPECT_FLOAT_EQ(player->Root().Position().y, SpawnOf(doc).y);
}

// 置物の出し直しのボタンを押すと、次の進行役の段で岩だけが置いた所へ戻り、自機はその場に残る
TEST(CourseDirector, RespawnButtonReturnsActorsButKeepsPlayer)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::Editor::SetActorJsonPosition(rock, NS::Vector3{5.0f, 1.0f, 0.0f});
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    NS::EnsureUniqueActorIds(doc);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);
    director->StartCourse();
    NS::SceneActor* placed = nullptr;
    for (NS::SceneActor* actor : scene.Actors())
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
        StepPlayer(player);
    }
    director->OnTick();

    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
    EXPECT_FLOAT_EQ(player->Root().Position().x, movedTo.x);
    EXPECT_FLOAT_EQ(player->Root().Position().z, movedTo.z);
}

TEST(CourseDirector, GoalStartsClearSequenceAndLocksInput)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json goal = NS::Editor::MakePrototypeJson<GL::Level::GoalActor>();
    NS::Editor::SetActorJsonPosition(goal, SpawnOf(doc));
    NS::SceneJsonActors(doc).push_back(std::move(goal));
    NS::EnsureUniqueActorIds(doc);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);

    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
    EXPECT_TRUE(scene.Fade().IsFading());
    const NS::PlayerInput* input = &player->Input();
    EXPECT_TRUE(input->IsLocked());

    // 流れの最中に届くゴールの知らせは捨てる
    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
}

// 止めた瞬間の向きを持ち続けると、暗転の間もゴールで倒していた向きへ歩き続ける
TEST(CourseDirector, ClearLockNeutralizesTheMoveInput)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    NS::Scene scene;
    scene.ResetFromJson(doc);
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        player->Update(false);
    }
    const float lockedSpeed = player->Body().LateralVelocity().Length();
    ASSERT_GT(lockedSpeed, 0.0f);

    player->Root().SetPosition(SpawnOf(doc));
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());

    for (int i = 0; i < 10; ++i)
    {
        StepPlayer(player);
    }
    EXPECT_FLOAT_EQ(player->DesiredDirection().Length(), 0.0f);
    EXPECT_FLOAT_EQ(player->DesiredSpeedScale(), 0.0f);
    // 床の無い空中では手を放しても横は減らないので、加速し続けないことを見る
    EXPECT_LE(player->Body().LateralVelocity().Length(), lockedSpeed);
}

TEST(CourseDirector, RestartReturnsActorsToTheirStart)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::Editor::SetActorJsonPosition(rock, NS::Vector3{5.0f, 1.0f, 0.0f});
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    NS::EnsureUniqueActorIds(doc);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene)->StartCourse();
    NS::SceneActor* placed = nullptr;
    for (NS::SceneActor* actor : scene.Actors())
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

TEST(CourseDirector, StartCourseStartsThePlayerAtRestFromThePlacedPose)
{
    NS::Scene scene;
    scene.ResetFromJson(CourseWithPlayer());
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 placedAt{5.0f, 5.0f, 5.0f};
    player->Root().SetPosition(placedAt);
    player->Body().SetVelocity(NS::Vector3{2.0f, 4.0f, 3.0f});

    NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(&scene)->StartCourse();

    // 自機は呼んだ時の姿から、やり直しと同じく速さ 0 で始まる
    EXPECT_FLOAT_EQ(player->Body().Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().z, 0.0f);
    EXPECT_FLOAT_EQ(player->Root().Position().x, placedAt.x);
    EXPECT_FLOAT_EQ(player->Root().Position().y, placedAt.y);
    EXPECT_FLOAT_EQ(player->Root().Position().z, placedAt.z);
}
