#include "Editor/EditorActors.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/Scene/SceneManager.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

// 落下死とゴールがメッセージで伝わり、GameScene がコースの流れを進めることを縛る

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

    // 本番と同じく、機器を読む PlayerActor の Update を 1 歩回す
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

TEST(GameScene, ClearSequenceUsesTheSceneFadeAndUnlocksInput)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    const NS::PlayerInput* input = &player->Input();
    const float duration = NS::OS::FrameTimer::FixedDelta() * 2.0f;
    ASSERT_TRUE(scene.NotifyGoal(duration, duration));

    scene.OnUpdate();
    ASSERT_TRUE(scene.IsClearing());
    EXPECT_TRUE(input->IsLocked());
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.OnUpdate();
    EXPECT_TRUE(scene.Fade().IsBlack());
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.0f);
    scene.OnUpdate();
    EXPECT_FALSE(scene.IsClearing());
    EXPECT_FALSE(input->IsLocked());
}

TEST(GameScene, GoalIsAcceptedOnlyOnTheFirstTouch)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    scene.StartPlay();
    const float duration = NS::OS::FrameTimer::FixedDelta() * 2.0f;

    // ゴールに触れている間は毎フレーム届く。ゴールの音を頼むのは、クリアを始めた最初の 1 回だけ
    EXPECT_TRUE(scene.NotifyGoal(duration, duration));
    EXPECT_FALSE(scene.NotifyGoal(duration, duration));
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsClearing());
    EXPECT_FALSE(scene.NotifyGoal(duration, duration));
}

TEST(GameScene, RestartDoesNotStartTheMusicAgain)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());

    EXPECT_TRUE(scene.StartMusic());
    scene.RestartCourse();
    EXPECT_FALSE(scene.StartMusic());

    // 止めた後は、次のプレイでまた始める
    scene.StopMusic();
    EXPECT_TRUE(scene.StartMusic());
}

// 曲の名前が空のシーン (読めずに立てた空のシーン) は、音の装置があっても曲を読まない
TEST(GameScene, StartPlayOfAnEmptySceneReadsNoMusic)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    NS::SceneManager scenes;
    scenes.SetAudioDevice(&device);
    const TestLogCapture log;

    NS::Scene& scene = scenes.LoadEmptyScene<GL::Level::GameScene>();

    ASSERT_NE(scene.GetAudioDevice(), nullptr);
    EXPECT_TRUE(scene.Environment().music.name.empty());
    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 当たりの下見のシーンは音の装置を受け取らないので、曲の名前があっても曲を読まない
TEST(GameScene, StartPlayWithoutAnAudioDeviceReadsNoMusic)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    nlohmann::json doc = CourseWithPlayer();
    NS::SceneEnvironment environment = NS::ReadSceneEnvironment(doc);
    environment.music = NS::SoundName{"missing_music"};
    NS::WriteSceneEnvironment(doc, environment);
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    ASSERT_EQ(scene.Environment().music.name, "missing_music");
    const TestLogCapture log;

    scene.StartPlay();

    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 止めた瞬間の向きを持ち続けると、暗転の間もゴールで倒していた向きへ歩き続ける
TEST(GameScene, ClearLockNeutralizesTheMoveInput)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);

    player->Input().SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        player->Update(false);
    }
    const float lockedSpeed = player->Body().LateralVelocity().Length();
    ASSERT_GT(lockedSpeed, 0.0f);

    player->Root().SetPosition(SpawnOf(doc));
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsClearing());

    for (int i = 0; i < 10; ++i)
    {
        StepPlayer(player);
    }
    EXPECT_FLOAT_EQ(player->Input().DesiredDirection().Length(), 0.0f);
    EXPECT_FLOAT_EQ(player->Input().DesiredSpeedScale(), 0.0f);
    // 床の無い空中では手を放しても横は減らないので、加速し続けないことを見る
    EXPECT_LE(player->Body().LateralVelocity().Length(), lockedSpeed);
}

TEST(GameScene, RestartReturnsActorsToTheirStart)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::Editor::SetActorJsonPosition(rock, NS::Vector3{5.0f, 1.0f, 0.0f});
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    NS::EnsureUniqueActorIds(doc);

    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
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

    scene.RestartCourse();
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
}

// 自機でないアクターが止め始めたヒットストップも、やり直しで解く。残すと出し直したアクターが止まったまま待つ
TEST(GameScene, RestartReleasesAHoldStartedByAnotherActor)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    NS::EnsureUniqueActorIds(doc);

    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    NS::SceneActor* placed = nullptr;
    for (NS::SceneActor* actor : scene.Actors())
    {
        if (std::string_view{actor->ClassName()} == "PropActor")
        {
            placed = actor;
        }
    }
    ASSERT_NE(placed, nullptr);
    scene.RequestStopScene(placed, 30);
    // 頼んだ止めは次の歩の始めに効く
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsSceneStopped());

    scene.RestartCourse();
    EXPECT_FALSE(scene.IsSceneStopped());
}

// リスポーンは自機を出し直さないので、自機が始めた止めは解かない
TEST(GameScene, RespawnKeepsAHoldStartedByThePlayer)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    scene.StartPlay();
    NS::SceneActor* player = nullptr;
    for (NS::SceneActor* actor : scene.Actors())
    {
        if (std::string_view{actor->ClassName()} == "PlayerActor")
        {
            player = actor;
        }
    }
    ASSERT_NE(player, nullptr);
    scene.RequestStopScene(player, 30);
    // 頼んだ止めは次の歩の始めに効く
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsSceneStopped());

    scene.RespawnActors();
    EXPECT_TRUE(scene.IsSceneStopped());
}

TEST(GameScene, StartPlayStartsThePlayerAtRestFromThePlacedPose)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 placedAt{5.0f, 5.0f, 5.0f};
    player->Root().SetPosition(placedAt);
    player->Body().SetVelocity(NS::Vector3{2.0f, 4.0f, 3.0f});

    scene.StartPlay();

    // 自機は呼んだ時の姿から、やり直しと同じく速さ 0 で始まる
    EXPECT_FLOAT_EQ(player->Body().Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().z, 0.0f);
    EXPECT_FLOAT_EQ(player->Root().Position().x, placedAt.x);
    EXPECT_FLOAT_EQ(player->Root().Position().y, placedAt.y);
    EXPECT_FLOAT_EQ(player->Root().Position().z, placedAt.z);
}

namespace
{
    // 自機と落下死の範囲に、x=5 の置物を足したシーン文書
    nlohmann::json CourseWithProp(nlohmann::json doc)
    {
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, "PropActor");
        NS::Editor::SetActorJsonPosition(rock, NS::Vector3{5.0f, 1.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        NS::EnsureUniqueActorIds(doc);
        return doc;
    }

    NS::SceneActor* FindProp(NS::Scene& scene)
    {
        for (NS::SceneActor* actor : scene.Actors())
        {
            if (std::string_view{actor->ClassName()} == "PropActor")
            {
                return actor;
            }
        }
        return nullptr;
    }
} // namespace

// 以下は知らせを歩 (scene.OnUpdate) の中で届かせ、その歩が終わった後のアクターの姿を見る。
// 流れを直に進めず歩で回すので、流れを進める置き場所が変わっても期待の値は変わらない

TEST(GameScene, DeathInAStepRestartsTheCourseBeforeTheNextStep)
{
    const nlohmann::json doc = CourseWithProp(CourseWithPlayer());
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    NS::SceneActor* placed = FindProp(scene);
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    // 落下死の範囲 (上面 y=-50) の中へ置く
    player->Root().SetPosition(NS::Vector3{0.0f, -52.0f, 0.0f});

    scene.OnUpdate();

    EXPECT_FALSE(player->IsDead());
    const NS::Vector3 spawn = SpawnOf(doc);
    EXPECT_FLOAT_EQ(player->Root().Position().x, spawn.x);
    EXPECT_FLOAT_EQ(player->Root().Position().y, spawn.y);
    EXPECT_FLOAT_EQ(player->Root().Position().z, spawn.z);
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
    EXPECT_FALSE(scene.IsClearing());
}

TEST(GameScene, RespawnButtonInAStepReturnsPropsBeforeTheNextStep)
{
    const nlohmann::json doc = CourseWithProp(CourseWithPlayer());
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    NS::SceneActor* placed = FindProp(scene);
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    const NS::Vector3 spawn = SpawnOf(doc);
    const NS::Vector3 movedTo{spawn.x + 3.0f, spawn.y, spawn.z + 3.0f};
    player->Root().SetPosition(movedTo);

    {
        const KeyPress press{NS::OS::Key::R};
        scene.OnUpdate();
    }

    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
    // 自機は戻さない。縦は重力で動くので横だけを見る
    EXPECT_FLOAT_EQ(player->Root().Position().x, movedTo.x);
    EXPECT_FLOAT_EQ(player->Root().Position().z, movedTo.z);
    EXPECT_FALSE(scene.IsClearing());
}

TEST(GameScene, GoalInAStepStartsTheClearAndRestartsBehindTheBlack)
{
    const nlohmann::json doc = CourseWithProp(CourseWithPlayerOnGoal());
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    NS::SceneActor* placed = FindProp(scene);
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});

    scene.OnUpdate();

    // クリアの流れは始まったが、やり直しは暗転しきるまで待つ
    ASSERT_TRUE(scene.IsClearing());
    EXPECT_TRUE(scene.Fade().IsFading());
    EXPECT_TRUE(player->Input().IsLocked());
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 9.0f);
    const NS::Vector3 spawn = SpawnOf(doc);
    EXPECT_FLOAT_EQ(player->Root().Position().x, spawn.x);
    EXPECT_FLOAT_EQ(player->Root().Position().z, spawn.z);

    for (int i = 0; i < 600 && scene.IsClearing(); ++i)
    {
        scene.OnUpdate();
    }
    EXPECT_FALSE(scene.IsClearing());
    EXPECT_FALSE(player->Input().IsLocked());
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
}

// エディタは同じシーンのオブジェクトでプレイを何度も始め、その間に ResetFromJson で組み直す。
// 組み直しで前のプレイの知らせが残ると、次のプレイの始めに頼んでいないやり直しやクリアが起きる
TEST(GameScene, RebuildingTheWorldDropsNoticesOfThePlay)
{
    const nlohmann::json doc = CourseWithProp(CourseWithPlayer());
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    const float duration = NS::OS::FrameTimer::FixedDelta() * 2.0f;
    scene.NotifyPlayerDead();
    scene.RequestRespawnActors();
    ASSERT_TRUE(scene.NotifyGoal(duration, duration));

    // 歩を回さずに組み直し、もう一度始める。前のプレイの知らせを戻すのは ResetFromJson
    scene.ResetFromJson(doc);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    NS::SceneActor* placed = FindProp(scene);
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    const NS::Vector3 spawn = SpawnOf(doc);
    const NS::Vector3 movedTo{spawn.x + 3.0f, spawn.y, spawn.z + 3.0f};
    player->Root().SetPosition(movedTo);

    scene.OnUpdate();

    EXPECT_FALSE(scene.IsClearing());
    EXPECT_FALSE(scene.Fade().IsFading());
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 9.0f);
    EXPECT_FLOAT_EQ(player->Root().Position().x, movedTo.x);
    EXPECT_FLOAT_EQ(player->Root().Position().z, movedTo.z);
}

TEST(GameScene, RebuildingTheWorldEndsTheClearOfThePlay)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayerOnGoal());
    scene.StartPlay();
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsClearing());

    scene.ResetFromJson(CourseWithPlayerOnGoal());

    EXPECT_FALSE(scene.IsClearing());
    EXPECT_FALSE(scene.Fade().IsFading());
    // 次のプレイの自機は操作できる
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    EXPECT_FALSE(player->Input().IsLocked());
}
