#include "Editor/EditorActors.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/ThirdPersonFollow.h"
#include "Game/PlayerActor.h"
#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
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
#include "TestSpawn.h"

#include <gtest/gtest.h>

#include <optional>

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

    // コースの始めの位置を MsgCourseStart で控え、MsgCourseRestart でそこへ戻る
    class CourseReturnActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(CourseReturnActor, NS::SceneActor)

    protected:
        bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override
        {
            if (NS::IsMsg<GL::Level::MsgCourseStart>(msg))
            {
                m_start = TransformObj().Position();
                return true;
            }
            if (NS::IsMsg<GL::Level::MsgCourseRestart>(msg))
            {
                // コースが始まった後に置かれた物は、戻る先を持たない
                if (!m_start.has_value())
                {
                    return false;
                }
                TransformObj().SetPosition(*m_start);
                return true;
            }
            return NS::SceneActor::ReceiveMsg(msg, sender, receiver);
        }

    private:
        std::optional<NS::Vector3> m_start;
    };

    // 組み直した後のシーンに、x=5 の CourseReturnActor を 1 体置く。StartPlay の前に呼ぶ
    NS::SceneActor* PlaceCourseReturnActor(NS::Scene& scene)
    {
        CourseReturnActor* placed = SpawnFromActorFile<CourseReturnActor>(scene, "CourseReturnActor");
        if (placed != nullptr)
        {
            placed->TransformObj().SetPosition(NS::Vector3{5.0f, 1.0f, 0.0f});
        }
        return placed;
    }
} // namespace

NS_CLASS(CourseReturnActor)

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
    EXPECT_FALSE(scene.Music().IsStarted());

    scene.StartPlay();
    ASSERT_TRUE(scene.Music().IsStarted());
    scene.RestartCourse();
    EXPECT_TRUE(scene.Music().IsStarted());

    // 組み直しでプレイを終えると止まり、次のプレイでまた始める
    scene.ResetFromJson(CourseWithPlayer());
    EXPECT_FALSE(scene.Music().IsStarted());
    scene.StartPlay();
    EXPECT_TRUE(scene.Music().IsStarted());
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
    ASSERT_NE(scene.Environment().music, nullptr);
    EXPECT_TRUE(scene.Environment().music->Path().empty());
    EXPECT_FALSE(scene.Environment().music->IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 当たりの下見のシーンは音の装置を受け取らないので、曲があっても曲を鳴らさない
TEST(GameScene, StartPlayWithoutAnAudioDeviceReadsNoMusic)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    nlohmann::json doc = CourseWithPlayer();
    NS::SceneEnvironment environment = NS::ReadSceneEnvironment(doc);
    environment.music = assets.LoadSound("Assets/Sounds/bgm_course.wav");
    NS::WriteSceneEnvironment(doc, environment);
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    ASSERT_NE(scene.Environment().music, nullptr);
    ASSERT_EQ(scene.Environment().music->Path(), "Assets/Sounds/bgm_course.wav");
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

    player->Input().SetStick(1.0f, 0.0f);
    for (int i = 0; i < 5; ++i)
    {
        player->Update(false);
    }
    const float lockedSpeed = player->Body().LateralVelocity().Length();
    ASSERT_GT(lockedSpeed, 0.0f);

    player->TransformObj().SetPosition(SpawnOf(doc));
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
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    NS::SceneActor* placed = PlaceCourseReturnActor(scene);
    ASSERT_NE(placed, nullptr);
    scene.StartPlay();
    placed->TransformObj().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});

    scene.RestartCourse();
    EXPECT_FLOAT_EQ(placed->TransformObj().Position().x, 5.0f);
}

// 自機でないアクターが止め始めたヒットストップも、やり直しで解く。残すと出し直したアクターが止まったまま待つ
TEST(GameScene, RestartReleasesAHoldStartedByAnotherActor)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json goal = NS::MakeActorJson();
    NS::SetActorJsonFile(goal, "GoalActor");
    NS::Editor::SetActorJsonPosition(goal, NS::Vector3{50.0f, 1.0f, 0.0f});
    NS::SceneJsonActors(doc).push_back(std::move(goal));
    NS::EnsureUniqueActorIds(doc);

    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    NS::SceneActor* placed = nullptr;
    for (NS::SceneActor* actor : scene.Actors())
    {
        if (std::string_view{actor->ClassName()} == "GoalActor")
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

// 同じ歩に自機が頼んで、まだ効いていない止めも、リスポーンで解かない
TEST(GameScene, RespawnKeepsAHoldThePlayerRequestedInTheSameStep)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    scene.RequestStopScene(player, 30);
    ASSERT_FALSE(scene.IsSceneStopped());

    scene.RespawnActors();
    scene.OnUpdate();

    EXPECT_TRUE(scene.IsSceneStopped());
}

// 出し直すアクターが頼んだ止めは、効く前でも解く
TEST(GameScene, RespawnDropsAHoldAnotherActorRequestedInTheSameStep)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    scene.StartPlay();
    const NS::SceneActor* other = nullptr;
    for (const NS::SceneActor* actor : scene.Actors())
    {
        if (std::string_view{actor->ClassName()} != "PlayerActor")
        {
            other = actor;
        }
    }
    ASSERT_NE(other, nullptr);
    scene.RequestStopScene(other, 30);

    scene.RespawnActors();
    scene.OnUpdate();

    EXPECT_FALSE(scene.IsSceneStopped());
}

// 組み直さずに始めるエディタのプレイでも、仮想カメラは今の初期の向きから始まる
TEST(GameScene, StartPlayPutsTheFollowCameraAtTheCurrentInitialPose)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    GL::Level::FollowCameraActor* follow = SpawnFromActorFile<GL::Level::FollowCameraActor>(scene, "FollowCameraActor");
    ASSERT_NE(follow, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&follow->Vcam(), nlohmann::json{{"初期ヨー", 1.0f}}), 0u);
    ASSERT_FLOAT_EQ(follow->Vcam().InitialYaw(), 1.0f);
    ASSERT_NE(follow->Vcam().Yaw(), 1.0f);

    scene.StartPlay();

    EXPECT_FLOAT_EQ(follow->Vcam().Yaw(), 1.0f);
}

TEST(GameScene, StartPlayStartsThePlayerAtRestFromThePlacedPose)
{
    GL::Level::GameScene scene;
    scene.ResetFromJson(CourseWithPlayer());
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 placedAt{5.0f, 5.0f, 5.0f};
    player->TransformObj().SetPosition(placedAt);
    player->Body().SetVelocity(NS::Vector3{2.0f, 4.0f, 3.0f});

    scene.StartPlay();

    // 自機は呼んだ時の姿から、やり直しと同じく速さ 0 で始まる
    EXPECT_FLOAT_EQ(player->Body().Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player->Body().Velocity().z, 0.0f);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().x, placedAt.x);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().y, placedAt.y);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().z, placedAt.z);
}

// 位置から鳴らす音を聞く点は、歩ごとに自機の位置になる。届く距離はシーンの設定の値を始めに渡す
TEST(GameScene, AStepPutsTheSoundListenerOnThePlayer)
{
    NS::Audio::AudioDevice device(false);
    nlohmann::json doc = CourseWithPlayer();
    NS::SceneEnvironment environment = NS::ReadSceneEnvironment(doc);
    environment.soundFullVolumeDistance = 25.0f;
    NS::WriteSceneEnvironment(doc, environment);
    GL::Level::GameScene scene;
    scene.SetAudioDevice(&device);
    scene.ResetFromJson(doc);
    NS::Audio::SoundScene* sounds = scene.GetSoundScene();
    ASSERT_NE(sounds, nullptr);
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    ASSERT_NE(sounds->Listener().position.y, player->TransformObj().Position().y);

    scene.StartPlay();
    EXPECT_FLOAT_EQ(sounds->FullVolumeDistance(), 25.0f);
    // 始めに置かれた聞く点から自機を離す。歩の中で置き直さなければ、下の比べが食い違う
    player->TransformObj().SetPosition(NS::Vector3{7.0f, 9.0f, 3.0f});
    ASSERT_NE(sounds->Listener().position.x, player->TransformObj().Position().x);
    scene.OnUpdate();

    EXPECT_FLOAT_EQ(sounds->Listener().position.x, player->TransformObj().Position().x);
    EXPECT_FLOAT_EQ(sounds->Listener().position.y, player->TransformObj().Position().y);
    EXPECT_FLOAT_EQ(sounds->Listener().position.z, player->TransformObj().Position().z);
}

// 0 以下の届く距離を書いたシーンは、既定の距離で読む。エディタの「曲を適用」が 0 以下を受けないため
TEST(GameScene, ASoundDistanceThatIsNotPositiveReadsAsTheDefault)
{
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneEnvironment environment;
    const float standard = environment.soundFullVolumeDistance;
    environment.soundFullVolumeDistance = 0.0f;
    NS::WriteSceneEnvironment(doc, environment);
    EXPECT_FLOAT_EQ(NS::ReadSceneEnvironment(doc).soundFullVolumeDistance, standard);

    environment.soundFullVolumeDistance = -3.0f;
    NS::WriteSceneEnvironment(doc, environment);
    EXPECT_FLOAT_EQ(NS::ReadSceneEnvironment(doc).soundFullVolumeDistance, standard);
}

// 以下は知らせを歩 (scene.OnUpdate) の中で届かせ、その歩が終わった後のアクターの姿を見る。
// 流れを直に進めず歩で回すので、流れを進める置き場所が変わっても期待の値は変わらない

TEST(GameScene, DeathInAStepRestartsTheCourseBeforeTheNextStep)
{
    const nlohmann::json doc = CourseWithPlayer();
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* placed = PlaceCourseReturnActor(scene);
    ASSERT_NE(placed, nullptr);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    placed->TransformObj().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    // 落下死の範囲 (上面 y=-50) の中へ置く
    player->TransformObj().SetPosition(NS::Vector3{0.0f, -52.0f, 0.0f});

    scene.OnUpdate();

    EXPECT_FALSE(player->IsDead());
    const NS::Vector3 spawn = SpawnOf(doc);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().x, spawn.x);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().y, spawn.y);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().z, spawn.z);
    EXPECT_FLOAT_EQ(placed->TransformObj().Position().x, 5.0f);
    EXPECT_FALSE(scene.IsClearing());
}

// やり直すまでのフレーム数を持つ範囲で死ぬと、その間は消えたままその場に残り、過ぎた歩でやり直す
TEST(GameScene, DeathZoneWithARestartDelayRestartsAfterThoseFrames)
{
    nlohmann::json doc = CourseWithPlayer();
    for (nlohmann::json& actor : NS::SceneJsonActors(doc))
    {
        if (NS::ActorJsonFile(actor) == "DeathZoneActor")
        {
            NS::ActorJsonClassObjs(actor)["Params"]["やり直すまでのフレーム数"] = 2;
        }
    }
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    player->TransformObj().SetPosition(NS::Vector3{0.0f, -52.0f, 0.0f});

    scene.OnUpdate();
    EXPECT_TRUE(player->IsDead());
    // 出現位置へ戻らず、範囲 (上面 y=-50) の中に残る
    EXPECT_LT(player->TransformObj().Position().y, -50.0f);
    scene.OnUpdate();
    EXPECT_TRUE(player->IsDead());

    scene.OnUpdate();
    EXPECT_FALSE(player->IsDead());
    EXPECT_FLOAT_EQ(player->TransformObj().Position().y, SpawnOf(doc).y);
}

TEST(GameScene, RespawnButtonInAStepReturnsOtherActorsBeforeTheNextStep)
{
    const nlohmann::json doc = CourseWithPlayer();
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* placed = PlaceCourseReturnActor(scene);
    ASSERT_NE(placed, nullptr);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    placed->TransformObj().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    const NS::Vector3 spawn = SpawnOf(doc);
    const NS::Vector3 movedTo{spawn.x + 3.0f, spawn.y, spawn.z + 3.0f};
    player->TransformObj().SetPosition(movedTo);

    {
        const KeyPress press{NS::OS::Key::R};
        scene.OnUpdate();
    }

    EXPECT_FLOAT_EQ(placed->TransformObj().Position().x, 5.0f);
    // 自機は戻さない。縦は重力で動くので横だけを見る
    EXPECT_FLOAT_EQ(player->TransformObj().Position().x, movedTo.x);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().z, movedTo.z);
    EXPECT_FALSE(scene.IsClearing());
}

TEST(GameScene, GoalInAStepStartsTheClearAndRestartsBehindTheBlack)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* placed = PlaceCourseReturnActor(scene);
    ASSERT_NE(placed, nullptr);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    placed->TransformObj().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});

    scene.OnUpdate();

    // クリアの流れは始まったが、やり直しは暗転しきるまで待つ
    ASSERT_TRUE(scene.IsClearing());
    EXPECT_TRUE(scene.Fade().IsFading());
    EXPECT_TRUE(player->Input().IsLocked());
    EXPECT_FLOAT_EQ(placed->TransformObj().Position().x, 9.0f);
    const NS::Vector3 spawn = SpawnOf(doc);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().x, spawn.x);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().z, spawn.z);

    for (int i = 0; i < 600 && scene.IsClearing(); ++i)
    {
        scene.OnUpdate();
    }
    EXPECT_FALSE(scene.IsClearing());
    EXPECT_FALSE(player->Input().IsLocked());
    EXPECT_FLOAT_EQ(placed->TransformObj().Position().x, 5.0f);
}

// エディタは同じシーンのオブジェクトでプレイを何度も始め、その間に ResetFromJson で組み直す。
// 組み直しで前のプレイの知らせが残ると、次のプレイの始めに頼んでいないやり直しやクリアが起きる
TEST(GameScene, RebuildingTheWorldDropsNoticesOfThePlay)
{
    const nlohmann::json doc = CourseWithPlayer();
    GL::Level::GameScene scene;
    scene.ResetFromJson(doc);
    scene.StartPlay();
    const float duration = NS::OS::FrameTimer::FixedDelta() * 2.0f;
    scene.NotifyPlayerDead(0);
    scene.RequestRespawnActors();
    ASSERT_TRUE(scene.NotifyGoal(duration, duration));

    // 歩を回さずに組み直し、もう一度始める。前のプレイの知らせを戻すのは ResetFromJson
    scene.ResetFromJson(doc);
    NS::SceneActor* placed = PlaceCourseReturnActor(scene);
    ASSERT_NE(placed, nullptr);
    scene.StartPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    placed->TransformObj().SetPosition(NS::Vector3{9.0f, 1.0f, 0.0f});
    const NS::Vector3 spawn = SpawnOf(doc);
    const NS::Vector3 movedTo{spawn.x + 3.0f, spawn.y, spawn.z + 3.0f};
    player->TransformObj().SetPosition(movedTo);

    scene.OnUpdate();

    EXPECT_FALSE(scene.IsClearing());
    EXPECT_FALSE(scene.Fade().IsFading());
    EXPECT_FLOAT_EQ(placed->TransformObj().Position().x, 9.0f);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().x, movedTo.x);
    EXPECT_FLOAT_EQ(player->TransformObj().Position().z, movedTo.z);
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
