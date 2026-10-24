#include "Editor/EditorMode.h"
#include "Editor/LevelEditorController.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Editor/Undo/ActorSnapshotCommand.h"
#include "Editor/Undo/UndoStack.h"
#include "Game/Game.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "TestRegistrations.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{
    // エディタで消す操作と同じく、今の姿を前に、居ない姿を後にしたコマンドを積む
    void DeleteActor(NS::Editor::UndoStack& undo, NS::Editor::ActorSnapshotApplier& applier, NS::uint32 id)
    {
        std::optional<nlohmann::json> before = applier.CaptureActor(id);
        ASSERT_TRUE(before.has_value());
        undo.Push(std::make_unique<NS::Editor::ActorSnapshotCommand>(id, std::move(before), std::nullopt), applier);
    }

    // 履歴が持つアクターを解放したかを見るための置物。JSON は PropActor として書くので、作り直すと PropActor になる
    class CountedProp final : public GL::Level::PropActor
    {
    public:
        ~CountedProp() override { ++s_destroyed; }

        void OnEndPlay() override
        {
            ++s_endPlays;
            GL::Level::PropActor::OnEndPlay();
        }

        static int s_endPlays;
        static int s_destroyed;
    };

    int CountedProp::s_endPlays = 0;
    int CountedProp::s_destroyed = 0;
} // namespace

// 消した操作を元に戻すと、消す前と同じアクターが同じ並びの位置に戻り、登録も消す前と同じ数になる
TEST(EditorUndoActor, UndoDeleteRestoresTheSameActor)
{
    NS::Scene scene;
    (void)scene.SpawnActor<GL::Level::PropActor>("A");
    GL::Level::PropActor* middle = scene.SpawnActor<GL::Level::PropActor>("B");
    (void)scene.SpawnActor<GL::Level::PropActor>("C");
    const NS::uint32 id = middle->Id();
    const std::size_t index = scene.Actors().IndexOfActorId(id);
    const Registrations before = CountRegistrations(scene);

    NS::Editor::ActorSnapshotApplier applier{&scene};
    NS::Editor::UndoStack undo;
    DeleteActor(undo, applier, id);
    ASSERT_EQ(scene.Actors().FindByActorId(id), nullptr);

    ASSERT_TRUE(undo.Undo(applier));
    EXPECT_EQ(scene.Actors().FindByActorId(id), middle);
    EXPECT_EQ(scene.Actors().IndexOfActorId(id), index);
    EXPECT_EQ(CountRegistrations(scene), before);
}

// 消した後に履歴を捨てると、消したアクターの登録が 1 つも残らない
TEST(EditorUndoActor, DiscardedHistoryLeavesNoRegistration)
{
    NS::Scene scene;
    // 当たりの画面と振動の管理クラスは持ち主が消えても残るので、残すアクターに先に作らせてから数える
    (void)scene.SpawnActor<GL::Level::PropActor>("Keep");
    const Registrations kept = CountRegistrations(scene);

    const NS::uint32 prop = scene.SpawnActor<GL::Level::PropActor>("PropActor")->Id();
    const NS::uint32 player = scene.SpawnActor<PlayerActor>("PlayerActor")->Id();
    const NS::uint32 light = scene.SpawnActor<NS::LightActor>("LightActor")->Id();
    const NS::uint32 camera = scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor")->Id();

    NS::Editor::ActorSnapshotApplier applier{&scene};
    NS::Editor::UndoStack undo;
    DeleteActor(undo, applier, prop);
    DeleteActor(undo, applier, player);
    DeleteActor(undo, applier, light);
    DeleteActor(undo, applier, camera);
    undo.Clear();

    EXPECT_EQ(CountRegistrations(scene), kept);
}

// 消してからプレイに入って戻ると、組み直しの前に履歴が持つアクターを解放し、元に戻すと JSON から作り直す
TEST(EditorUndoActor, UndoDeleteAfterLeavingPlayRebuildsFromJson)
{
    NS::Scene scene;
    (void)scene.SpawnActor<GL::Level::PropActor>("PropActor");
    const NS::uint32 id = scene.SpawnActor<CountedProp>("PropActor")->Id();
    LevelEditorController editor(&scene);
    const Registrations before = CountRegistrations(scene);
    CountedProp::s_endPlays = 0;
    CountedProp::s_destroyed = 0;

    NS::Editor::ActorSnapshotApplier applier{&scene};
    DeleteActor(editor.Editor().Undo(), applier, id);
    editor.EnterPlay();
    editor.EnterEdit();

    // 組み直しの後に履歴が持ったままだと、捨てた管理クラスを指したアクターを戻してしまう
    ASSERT_EQ(CountedProp::s_destroyed, 1);
    EXPECT_EQ(CountedProp::s_endPlays, 1);

    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    const NS::SceneActor* restored = scene.Actors().FindByActorId(id);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(CountedProp::s_destroyed, 1);
    EXPECT_EQ(CountRegistrations(scene), before);
}

// 消してからレベルを開くと、読み込んだシーンを組み直す前に、履歴が持つアクターを解放する
TEST(EditorUndoActor, OpenLevelFreesHeldActorsBeforeRebuild)
{
    NS::Scene scene;
    const NS::uint32 id = scene.SpawnActor<CountedProp>("PropActor")->Id();
    CountedProp::s_endPlays = 0;
    CountedProp::s_destroyed = 0;

    NS::Editor::ActorSnapshotApplier applier{&scene};
    NS::Editor::EditorMode editor;
    editor.SetApplier(&applier);
    DeleteActor(editor.Undo(), applier, id);

    int destroyedAtRebuild = -1;
    int endPlaysAtRebuild = -1;
    editor.SetLoadLevelFn([&destroyedAtRebuild, &endPlaysAtRebuild](nlohmann::json&&) {
        destroyedAtRebuild = CountedProp::s_destroyed;
        endPlaysAtRebuild = CountedProp::s_endPlays;
    });
    ASSERT_TRUE(editor.OpenLevel(NS::Editor::LevelNameOfScenePath(::Game::StartScenePath())));

    EXPECT_EQ(destroyedAtRebuild, 1);
    EXPECT_EQ(endPlaysAtRebuild, 1);
    EXPECT_EQ(editor.Undo().UndoSize(), 0u);
}
