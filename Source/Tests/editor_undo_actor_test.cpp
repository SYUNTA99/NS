#include "Editor/EditorMode.h"
#include "Editor/EditorController.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/EditorActors.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/Undo/ActorSnapshotCommand.h"
#include "Editor/Undo/UndoStack.h"
#include "Game/Level/GameScene.h"
#include "Game/Game.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "TestRegistrations.h"
#include "TestSpawn.h"
#include "Tests/TestCheckLayer.h"
#include "Tests/TestEditorEdits.h"
#include "Tests/TestHiddenApp.h"
#include "Tests/TestSceneSetup.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using NS::Ed::EditorController;

namespace
{
    // エディタで消す操作と同じく、今の姿を前に、居ない姿を後にしたコマンドを積む
    void DeleteActor(NS::Ed::UndoStack& undo, NS::Scene& scene, NS::uint32 id)
    {
        std::optional<nlohmann::json> before = NS::Ed::CaptureActor(scene.Actors(), id);
        ASSERT_TRUE(before.has_value());
        undo.Push(std::make_unique<NS::Ed::ActorSnapshotCommand>(scene, id, std::move(before), std::nullopt));
    }

    // 履歴が持つアクターを解放したかを見るためのゴール。数えるのは counted を立てた物だけで、
    // JSON から作り直した CountedGoal は数えない
    class CountedGoal final : public GL::Level::GoalActor
    {
    public:
        NS_REFLECT_NONE(CountedGoal, GL::Level::GoalActor)

        ~CountedGoal() override
        {
            if (counted)
            {
                ++s_destroyed;
            }
        }

        void OnEndPlay() override
        {
            if (counted)
            {
                ++s_endPlays;
            }
            GL::Level::GoalActor::OnEndPlay();
        }

        bool counted = false;
        static int s_endPlays;
        static int s_destroyed;
    };

    int CountedGoal::s_endPlays = 0;
    int CountedGoal::s_destroyed = 0;

    // GoalActor の .actor をクラスだけ替えて CountedGoal の .actor に敷き、数える CountedGoal を作って id を返す
    NS::uint32 SpawnCountedGoal(NS::Scene& scene)
    {
        const nlohmann::json* goal = NS::ActorFileLibrary::Get().Find("GoalActor");
        EXPECT_NE(goal, nullptr);
        if (goal == nullptr)
        {
            return NS::k_NoActorId;
        }
        nlohmann::json actorFile = *goal;
        NS::SetActorFileClass(actorFile, "CountedGoal");
        EXPECT_TRUE(NS::ActorFileLibrary::Get().Set("CountedGoal", std::move(actorFile)));
        CountedGoal* spawned = SpawnFromActorFile<CountedGoal>(scene, "CountedGoal", "GoalActor");
        EXPECT_NE(spawned, nullptr);
        if (spawned == nullptr)
        {
            return NS::k_NoActorId;
        }
        spawned->counted = true;
        return spawned->Id();
    }
} // namespace

NS_CLASS(CountedGoal)

// 消した操作を元に戻すと、消す前と同じアクターが同じ並びの位置に戻り、登録も消す前と同じ数になる
TEST(EditorUndoActor, UndoDeleteRestoresTheSameActor)
{
    NS::Scene scene;
    (void)SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor", "A");
    GL::Level::GoalActor* middle = SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor", "B");
    (void)SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor", "C");
    const NS::uint32 id = middle->Id();
    const std::size_t index = scene.Actors().IndexOfActorId(id);
    const Registrations before = CountRegistrations(scene);

    NS::Ed::UndoStack undo;
    DeleteActor(undo, scene, id);
    ASSERT_EQ(scene.Actors().FindByActorId(id), nullptr);

    ASSERT_TRUE(undo.Undo());
    EXPECT_EQ(scene.Actors().FindByActorId(id), middle);
    EXPECT_EQ(scene.Actors().IndexOfActorId(id), index);
    EXPECT_EQ(CountRegistrations(scene), before);
}

// 消した後に履歴を捨てると、消したアクターの登録が 1 つも残らない
TEST(EditorUndoActor, DiscardedHistoryLeavesNoRegistration)
{
    NS::Scene scene;
    // 当たりの画面と振動の管理クラスは持ち主が消えても残るので、残すアクターに先に作らせてから数える
    (void)SpawnFromActorFile<PlayerActor>(scene, "PlayerActor", "Keep");
    const Registrations kept = CountRegistrations(scene);

    const NS::uint32 goal = SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor")->Id();
    const NS::uint32 player = SpawnFromActorFile<PlayerActor>(scene, "PlayerActor")->Id();
    const NS::uint32 light = SpawnFromActorFile<NS::LightActor>(scene, "LightActor")->Id();
    const NS::uint32 camera = SpawnFromActorFile<GL::Level::FollowCameraActor>(scene, "FollowCameraActor")->Id();

    NS::Ed::UndoStack undo;
    DeleteActor(undo, scene, goal);
    DeleteActor(undo, scene, player);
    DeleteActor(undo, scene, light);
    DeleteActor(undo, scene, camera);
    undo.Clear();

    EXPECT_EQ(CountRegistrations(scene), kept);
}

// 消してからプレイに入って戻ると、組み直しの前に履歴が持つアクターを解放し、元に戻すと JSON から作り直す
TEST(EditorUndoActor, UndoDeleteAfterLeavingPlayRebuildsFromJson)
{
    GL::Level::GameScene scene;
    (void)SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor");
    const NS::uint32 id = SpawnCountedGoal(scene);
    EditorController editor(scene);
    const Registrations before = CountRegistrations(scene);
    CountedGoal::s_endPlays = 0;
    CountedGoal::s_destroyed = 0;

    DeleteActor(editor.Editor().Undo(), scene, id);
    editor.EnterPlay();
    editor.EnterEdit();

    // 組み直しの後に履歴が持ったままだと、捨てた管理クラスを指したアクターを戻してしまう
    ASSERT_EQ(CountedGoal::s_destroyed, 1);
    EXPECT_EQ(CountedGoal::s_endPlays, 1);

    ASSERT_TRUE(editor.Editor().Undo().Undo());
    const NS::SceneActor* restored = scene.Actors().FindByActorId(id);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(CountedGoal::s_destroyed, 1);
    EXPECT_EQ(CountRegistrations(scene), before);
}

// 消してからレベルを開くと、読み込んだシーンを組み直す前に、履歴が持つアクターを解放する
TEST(EditorUndoActor, OpenLevelFreesHeldActorsBeforeRebuild)
{
    NS::Scene scene;
    const NS::uint32 id = SpawnCountedGoal(scene);
    CountedGoal::s_endPlays = 0;
    CountedGoal::s_destroyed = 0;

    NS::Ed::EditorMode editor;
    DeleteActor(editor.Undo(), scene, id);

    int destroyedAtRebuild = -1;
    int endPlaysAtRebuild = -1;
    editor.SetLoadLevelFn([&destroyedAtRebuild, &endPlaysAtRebuild](nlohmann::json&&) {
        destroyedAtRebuild = CountedGoal::s_destroyed;
        endPlaysAtRebuild = CountedGoal::s_endPlays;
    });
    ASSERT_TRUE(editor.OpenLevel(NS::Ed::StartLevelName()));

    EXPECT_EQ(destroyedAtRebuild, 1);
    EXPECT_EQ(endPlaysAtRebuild, 1);
    EXPECT_EQ(editor.Undo().UndoSize(), 0u);
}

// 編集の操作ごとに、Ctrl+Z を押すと操作の前のシーンへ、続けて Ctrl+Y を押すと操作の後のシーンへ戻る
// .actor の項目は ActorFile.UndoingAnUnmarkedFieldEditLeavesTheSceneSaved が見る
TEST(EditorUndoActor, CtrlZAndCtrlYUndoAndRedoEachEdit)
{
    NS::Application app(HiddenAppDesc("Ctrl+Z と Ctrl+Y の試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        struct Edit
        {
            const char* name;
            std::function<void(EditorController&, NS::Scene&)> run;
        };
        const std::string stone =
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Materials/stone.mat");
        const std::vector<Edit> edits{
            {"置く",
             [](EditorController& editor, NS::Scene&) {
                 const std::optional<NS::Ed::PlacementItem> item = NS::Ed::FindPlacementItem("GoalActor");
                 ASSERT_TRUE(item.has_value());
                 editor.PlaceItem(*item);
             }},
            {"消す",
             [](EditorController& editor, NS::Scene&) {
                 editor.SelectActorById(1);
                 editor.DeleteSelectedActor();
             }},
            // 取り消しが消した順と逆に戻さないと、2 体の並びが入れ替わる
            {"まとめて消す",
             [](EditorController& editor, NS::Scene&) {
                 editor.SelectActors({1, 2}, 1);
                 editor.DeleteSelectedActor();
             }},
            {"ギズモで動かす",
             [](EditorController& editor, NS::Scene& scene) {
                 editor.SelectActorById(1);
                 NS::SceneActor* actor = scene.Actors().FindByActorId(1);
                 ASSERT_NE(actor, nullptr);
                 editor.BeginTransformEdit();
                 actor->TransformObj().SetPosition(NS::Vector3{4.0f, 0.0f, 0.0f});
                 editor.CommitTransformEdit();
             }},
            {"インスペクタの値を変える",
             [](EditorController& editor, NS::Scene& scene) {
                 editor.SelectActorById(1);
                 NS::SceneActor* actor = scene.Actors().FindByActorId(1);
                 ASSERT_NE(actor, nullptr);
                 editor.BeginClassObjEdit();
                 WritePoseField(editor, actor, NS::k_PositionFieldName, NS::Vector3{0.0f, 3.0f, 0.0f});
                 editor.FinishFieldWrite(actor, NS::FieldType::Vector3);
                 editor.CommitClassObjEdit();
             }},
            {"素材を付ける",
             [&stone](EditorController& editor, NS::Scene&) {
                 editor.SelectActorById(1);
                 EXPECT_TRUE(editor.ApplyMaterialToSelected(stone));
             }},
            {"改名", [](EditorController& editor, NS::Scene&) { editor.RenameActor(1, "改名した"); }},
            {"親を付け替える",
             [](EditorController& editor, NS::Scene&) { EXPECT_TRUE(editor.ReparentActor(2, 1)); }},
            {"シーン設定",
             [](EditorController& editor, NS::Scene&) {
                 EXPECT_TRUE(editor.SetSceneMusic(
                     "Assets/Sounds/bgm_course.wav", 0.4f, editor.SceneSoundFullVolumeDistance()));
             }},
        };

        // 次に振る id は取り消しても戻さない。消したアクターの id を、後から置くアクターに使い回さないため
        const auto shape = [](const NS::Scene& scene) {
            nlohmann::json saved = scene.ToJson();
            saved.erase("nextObjectId");
            return saved;
        };
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        for (const Edit& edit : edits)
        {
            nlohmann::json doc = NS::MakeSceneJson();
            AddActorJson(doc, "GoalActor", 1);
            AddActorJson(doc, "GoalActor", 2);
            NS::Scene scene;
            scene.SetSimulationEnabled(false);
            scene.ResetFromJson(doc);
            EditorController editor(scene);
            editor.Setup({});
            keyboard.ClearState();
            editor.Tick();

            const nlohmann::json before = shape(scene);
            edit.run(editor, scene);
            const nlohmann::json after = shape(scene);
            // 変わらなかった操作だけを飛ばし、表の残りの操作と後の押していない状態へ戻す所は続ける
            EXPECT_NE(after, before) << edit.name;
            if (after == before)
            {
                continue;
            }

            PressCtrlKey(editor, keyboard, NS::OS::Key::Z);
            EXPECT_EQ(shape(scene), before) << edit.name;
            PressCtrlKey(editor, keyboard, NS::OS::Key::Y);
            EXPECT_EQ(shape(scene), after) << edit.name;
        }
        // 共有の入力を押していない状態へ戻す。残すと後の試しが鍵を読む
        keyboard.ClearState();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// Model のメッシュの欄を変えて Ctrl+Z を押すと、欄のメッシュの名前も描くメッシュも変える前の物に戻る。
// 自機は描くメッシュを PlayerAppearance が差し替えるので、ゴールとは別に見る
TEST(EditorUndoActor, CtrlZRestoresTheModelMeshAndTheDrawnMesh)
{
    NS::Application app(HiddenAppDesc("メッシュの取り消しの試し", {160, 90}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::OS::Keyboard& keyboard = app.Input().Keyboard();
        for (const char* actorFile : {"GoalActor", "PlayerActor"})
        {
            nlohmann::json doc = NS::MakeSceneJson();
            AddActorJson(doc, actorFile, 1);
            NS::Scene scene;
            scene.SetSimulationEnabled(false);
            scene.ResetFromJson(doc);
            EditorController editor(scene);
            editor.Setup({});
            keyboard.ClearState();
            editor.Tick();

            NS::SceneActor* actor = scene.Actors().FindByActorId(1);
            ASSERT_NE(actor, nullptr) << actorFile;
            NS::Model* model = actor->ModelClassObj();
            ASSERT_NE(model, nullptr) << actorFile;
            const std::string meshBefore = model->MeshRef();
            const NS::Mesh* drawnBefore = model->GetMesh();
            ASSERT_NE(meshBefore, "wedge45") << actorFile;

            // インスペクタの部品の一覧の行と同じ道で、メッシュの欄に道を書いて確定する
            const NS::ClassObjInfo* row = nullptr;
            NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& candidate, const NS::Object* part) {
                if (part == model)
                {
                    row = &candidate;
                }
            });
            ASSERT_NE(row, nullptr) << actorFile;
            const NS::FieldDesc* field = NS::FindField(model->GetReflection(), "メッシュ");
            ASSERT_NE(field, nullptr) << actorFile;
            editor.SelectActorById(1);
            editor.BeginClassObjEdit();
            const std::string wedge = "wedge45";
            ASSERT_EQ(field->set(model, &wedge), NS::FieldWriteResult::Written) << actorFile;
            editor.NoteFieldEdit(actor, *row, "メッシュ");
            editor.FinishFieldWrite(actor, NS::FieldType::Asset);
            editor.CommitClassObjEdit();
            ASSERT_EQ(model->MeshRef(), "wedge45") << actorFile;
            // 自機の描くメッシュは PlayerAppearance の姿のメッシュなので、欄を変えても変わらない
            if (std::string_view{actorFile} == "GoalActor")
            {
                ASSERT_NE(model->GetMesh(), drawnBefore);
            }

            PressCtrlKey(editor, keyboard, NS::OS::Key::Z);
            const NS::SceneActor* restored = scene.Actors().FindByActorId(1);
            ASSERT_NE(restored, nullptr) << actorFile;
            const NS::Model* restoredModel = restored->ModelClassObj();
            ASSERT_NE(restoredModel, nullptr) << actorFile;
            EXPECT_EQ(restoredModel->MeshRef(), meshBefore) << actorFile;
            EXPECT_EQ(restoredModel->GetMesh(), drawnBefore) << actorFile;
        }
        keyboard.ClearState();
    }));
    EXPECT_EQ(app.Run(), 0);
}
