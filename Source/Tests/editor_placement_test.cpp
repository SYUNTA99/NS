#include "Editor/CategoryPalette.h"
#include "Editor/EditorActors.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/GoalActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"
#include "TestSpawn.h"
#include "Tests/TestCheckLayer.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include "Tests/TestHiddenApp.h"
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

using NS::Editor::LevelEditorController;

// エディタの配置 (ヒエラルキーの追加メニュー・メッシュの投げ込み) が、SceneActor のクラスごと置くことを縛る

TEST(EditorPlacement, PlaceItemSpawnsActorOfItsClassAndSelectsIt)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);

    const NS::uint32 id = editor.SelectedActorId();
    ASSERT_NE(id, NS::k_NoActorId);
    NS::SceneActor* placed = scene.Actors().FindByActorId(id);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "GoalActor");
}

TEST(EditorPlacement, PlacedPartsUseTheItemsMesh)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    ASSERT_TRUE(item.has_value());

    editor.PlaceItem(*item);

    NS::SceneActor* placed = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapPartsActor");
    const NS::Model* renderer = placed->ModelClassObj();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "wedge45");
    // 当たりは見た目のメッシュに付いて来る
    EXPECT_NE(NS::Cast<NS::MeshCollision>(placed->CollisionClassObj()), nullptr);
}

// 坂の角度は組み込みの形の表だけが持つ。ひな形とパレットのブラシは、その表の角度を読む
TEST(EditorPlacement, SlopeAnglesComeFromTheBuiltinShapes)
{
    for (const std::string_view name : {"wedge45", "wedge30", "wedge22", "wedge15"})
    {
        const std::optional<float> angle = NS::AssetManager::BuiltinSlopeAngleDegrees(name);
        ASSERT_TRUE(angle.has_value()) << name;
        EXPECT_FLOAT_EQ(NS::Editor::PartsSlopeAngleDegrees(NS::Editor::MakeMeshPartsPrototype(name)), *angle)
            << name;
    }
    EXPECT_FALSE(NS::AssetManager::BuiltinSlopeAngleDegrees("cube").has_value());

    NS::Editor::CategoryPalette palette;
    palette.SetActiveSlot(1);
    EXPECT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), *NS::AssetManager::BuiltinSlopeAngleDegrees("wedge45"));
}

// エディタの姿勢の関数で JSON に書いた姿が、そのまま組んだアクターの根の姿になる
TEST(EditorPlacement, PoseFunctionsWriteTheJsonTheActorReads)
{
    nlohmann::json bare = NS::MakeActorJson();
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(bare).y, 1.0f);
    NS::Editor::EnsureTransformClassObj(bare);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(bare).y, 1.0f);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(bare).y, 0.0f);

    nlohmann::json object = NS::Editor::MakePrototypeJson<GL::Level::GoalActor>();
    const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(0.5f, 0.0f, 0.0f);
    NS::Editor::SetActorJsonPosition(object, NS::Vector3{1.0f, 2.0f, 3.0f});
    NS::Editor::SetActorJsonRotation(object, rotation);
    NS::Editor::SetActorJsonScale(object, NS::Vector3{2.0f, 2.0f, 2.0f});
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(object).z, 3.0f);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonRotation(object).y, rotation.y);
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonScale(object).x, 2.0f);

    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(actor->TransformObj().Position().z, 3.0f);
    EXPECT_FLOAT_EQ(actor->TransformObj().Rotation().y, rotation.y);
    EXPECT_FLOAT_EQ(actor->TransformObj().Scale().x, 2.0f);
}

// この試しは置いた物の種類と欄の名前だけを見るので、実在しない file で足りる
TEST(EditorPlacement, DroppedMeshBecomesMapPartsWithMeshCollision)
{
    NS::Scene scene;
    LevelEditorController editor(scene);

    const TestLogCapture log;
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    EXPECT_EQ(log.ErrorCount(), 2);

    NS::SceneActor* placed = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(std::string_view{placed->ClassName()}, "MapPartsActor");
    const NS::Model* renderer = placed->ModelClassObj();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "Assets/Models/__ns_missing_terrain__.glb");
    EXPECT_NE(NS::Cast<NS::MeshCollision>(placed->CollisionClassObj()), nullptr);
    // 名前はファイル名から付く
    EXPECT_EQ(placed->Name(), "__ns_missing_terrain__");
}

// 1 m の升として数えるのは組み込みの立方体と坂だけ。投げ込んだメッシュは大きさが分からないので数えない
TEST(EditorPlacement, OnlyTheBuiltInPartsCountAsCells)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const std::optional<NS::Editor::PlacementItem> cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    ASSERT_TRUE(cube.has_value());
    editor.PlaceItem(*cube);
    const NS::SceneActor* placedCube = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(placedCube, nullptr);
    editor.AddMeshParts(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Models"),
        "__ns_missing_terrain__.glb"));
    const NS::SceneActor* dropped = scene.Actors().FindByActorId(editor.SelectedActorId());
    ASSERT_NE(dropped, nullptr);

    EXPECT_TRUE(NS::Editor::IsCellBrushActor(placedCube));
    EXPECT_FALSE(NS::Editor::IsCellBrushActor(dropped));
}

namespace
{
    // 消したアクターを、その場で OnEndPlay を呼んで解放したかを見るためのゴール。
    // 数えるのは counted を立てた物だけで、JSON から作り直した CountedGoal は数えない
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
    // 敷いた .actor は outScope が試しの終わりに外す
    NS::uint32 SpawnCountedGoal(NS::Scene& scene, std::optional<ScopedActorFile>& outScope)
    {
        CountedGoal::s_endPlays = 0;
        CountedGoal::s_destroyed = 0;
        const nlohmann::json* goal = NS::ActorFileLibrary::Get().Find("GoalActor");
        EXPECT_NE(goal, nullptr);
        if (goal == nullptr)
        {
            return NS::k_NoActorId;
        }
        nlohmann::json actorFile = *goal;
        NS::SetActorFileClass(actorFile, "CountedGoal");
        outScope.emplace("CountedGoal", std::move(actorFile));
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

// 置く・筆で回す・消す は、どれも未保存にする。読み込んだだけのシーンは未保存でない
TEST(EditorUnsaved, PlaceRotateAndDeleteMarkTheSceneUnsaved)
{
    {
        NS::Scene scene;
        LevelEditorController editor(scene);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("GoalActor");
        ASSERT_TRUE(item.has_value());
        editor.PlaceItem(*item);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }
    {
        // 升に置いた立方体を、読み込んだシーンとして始める
        NS::Scene source;
        LevelEditorController placing(source);
        placing.Editor().Palette().SetActiveSlot(0);
        placing.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        const nlohmann::json loaded = source.ToJson();

        NS::Scene scene;
        scene.ResetFromJson(loaded);
        LevelEditorController editor(scene);
        ASSERT_NE(NS::Editor::FindActorIdAtCell(scene.Actors(), 0, 0, 0), NS::k_NoActorId);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());
        editor.Editor().RotateAtProgrammatic(0, 0, 0);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }
    {
        NS::Scene scene;
        const NS::uint32 id = SpawnFromActorFile<GL::Level::GoalActor>(scene, "GoalActor")->Id();
        LevelEditorController editor(scene);
        ASSERT_FALSE(editor.Editor().HasUnsavedChanges());
        editor.SelectActorById(id);
        editor.DeleteSelectedActor();
        EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);
        EXPECT_TRUE(editor.Editor().HasUnsavedChanges());
    }
}

// エディタで消したアクターは、その場で OnEndPlay を呼んで解放し、一覧から無くなる
TEST(EditorUnsaved, DeletedActorIsEndedAndFreedAtOnce)
{
    std::optional<ScopedActorFile> scope;
    NS::Scene scene;
    const NS::uint32 id = SpawnCountedGoal(scene, scope);
    ASSERT_NE(id, NS::k_NoActorId);
    LevelEditorController editor(scene);
    editor.SelectActorById(id);

    editor.DeleteSelectedActor();

    EXPECT_EQ(scene.Actors().FindByActorId(id), nullptr);
    EXPECT_EQ(CountedGoal::s_endPlays, 1);
    EXPECT_EQ(CountedGoal::s_destroyed, 1);
}

namespace
{
    // 升 (0, 0, 0) にパレットの slot の物を筆で置き、そこに居るアクターの id を返す
    NS::uint32 PaintCell(LevelEditorController& editor, NS::Scene& scene, std::size_t slot)
    {
        editor.Editor().Palette().SetActiveSlot(slot);
        editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
        return NS::Editor::FindActorIdAtCell(scene.Actors(), 0, 0, 0);
    }

    // パレットの slot の物の .actor の名前
    std::string SlotActorFile(LevelEditorController& editor, std::size_t slot)
    {
        editor.Editor().Palette().SetActiveSlot(slot);
        return NS::ReadActorHeader(editor.Editor().Palette().CurrentTemplate()).actorFile;
    }
} // namespace

// 升を回した向きは、シーンを書き出して読み直しても残る
TEST(EditorDirectEdit, RotatedCellKeepsItsRotationAfterReload)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const NS::uint32 id = PaintCell(editor, scene, 0);
    ASSERT_NE(id, NS::k_NoActorId);

    editor.Editor().RotateAtProgrammatic(0, 0, 0);
    const NS::SceneActor* rotated = scene.Actors().FindByActorId(id);
    ASSERT_NE(rotated, nullptr);
    const NS::Quaternion expected = rotated->TransformObj().Rotation();
    // 回っていなければ、読み直しで残るかを見分けられない
    ASSERT_GT(std::abs(expected.y), 0.1f);

    NS::Scene reloaded;
    reloaded.ResetFromJson(scene.ToJson());
    const NS::SceneActor* loaded = reloaded.Actors().FindByActorId(id);
    ASSERT_NE(loaded, nullptr);
    const NS::Quaternion actual = loaded->TransformObj().Rotation();
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
    EXPECT_NEAR(actual.w, expected.w, 1e-5f);
}

// 物のある升へ別の形を筆で置くと、同じ id のまま新しい形になり、読み直しても残る
TEST(EditorDirectEdit, PaintingOverACellReplacesItWithTheSameId)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const NS::uint32 id = PaintCell(editor, scene, 0);
    ASSERT_NE(id, NS::k_NoActorId);

    // 枠 0 と 1 は同じ .actor でメッシュだけが違う。置き換わったかはメッシュで見分ける
    const std::string expected = SlotActorFile(editor, 1);

    EXPECT_EQ(PaintCell(editor, scene, 1), id);

    NS::Scene reloaded;
    reloaded.ResetFromJson(scene.ToJson());
    const NS::SceneActor* loaded = reloaded.Actors().FindByActorId(id);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->ActorFileName(), expected);
    ASSERT_NE(loaded->ModelClassObj(), nullptr);
    EXPECT_EQ(loaded->ModelClassObj()->MeshRef(), "wedge45");
}

// 升の物をクラスの違う物 (ゴール) で置き換えても、同じ id のままそのクラスになり、読み直しても残る
TEST(EditorDirectEdit, PaintingADifferentClassOverACellReplacesIt)
{
    NS::Scene scene;
    LevelEditorController editor(scene);
    const NS::uint32 id = PaintCell(editor, scene, 0);
    ASSERT_NE(id, NS::k_NoActorId);

    const std::string before = SlotActorFile(editor, 0);
    const std::string expected = SlotActorFile(editor, 2);
    ASSERT_NE(expected, before);

    editor.Editor().PlaceUnderCursorProgrammatic(0, 0, 0);
    const NS::SceneActor* replaced = scene.Actors().FindByActorId(id);
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(std::string_view{replaced->ClassName()}, "GoalActor");
    EXPECT_EQ(replaced->ActorFileName(), expected);

    NS::Scene reloaded;
    reloaded.ResetFromJson(scene.ToJson());
    const NS::SceneActor* loaded = reloaded.Actors().FindByActorId(id);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(std::string_view{loaded->ClassName()}, "GoalActor");
    EXPECT_EQ(loaded->ActorFileName(), expected);
}

