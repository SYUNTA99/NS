#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/LevelEditorController.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Level/ScreenFade.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>
#include <utility>

//! Application に依存しない Scene + LevelEditorController でモード切替と Undo 履歴の保持を確かめる
//! Setup / OnStart は Application::Get() が要るので呼ばない。player 等は nullptr のまま
//! 世界を回す / 止める / 一時停止の実体はシーンのスイッチで、controller はそれを操作する

namespace
{
    // 接触クリアの印だけを持つゴールを組む
    NS::Obj::ObjectData MakeGoal(float x, float y, float z)
    {
        NS::Obj::ObjectData object;
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{x, y, z});
        object.components.push_back(NS::Obj::MakeComponentEntry("Goal"));
        return object;
    }

    // プレイヤーと同じ場所に置くトリガの hazard 箱を組む
    NS::Obj::ObjectData MakeTriggerHazard()
    {
        NS::Obj::ObjectData object;
        nlohmann::json box = NS::Obj::MakeComponentEntry("BoxCollider");
        NS::Obj::SetField(box, "トリガー", true);
        object.components = nlohmann::json::array({std::move(box), NS::Obj::MakeComponentEntry("Hazard")});
        return object;
    }

    NS::Obj::ObjectData MakeRock(float x, float y, float z)
    {
        NS::Obj::ObjectData object;
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{x, y, z});
        object.components.push_back(NS::Obj::MakeComponentEntry("BoxCollider"));
        return object;
    }

    NS::Obj::GameObject* FindFirstPlaced(NS::Obj::ObjectList& objects)
    {
        for (NS::Obj::GameObject* obj : objects)
        {
            if (!obj->IsTransient())
                return obj;
        }
        return nullptr;
    }

    void SetFloatField(NS::Obj::Component& comp, std::string_view name, float value)
    {
        const NS::Obj::ReflectionInfo* info = comp.GetReflection();
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} == name)
            {
                info->fields[i].set(&comp, &value);
                return;
            }
        }
    }

    float GetFloatField(const NS::Obj::Component& comp, std::string_view name)
    {
        const NS::Obj::ReflectionInfo* info = comp.GetReflection();
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} == name)
            {
                float value = 0.0f;
                info->fields[i].get(&comp, &value);
                return value;
            }
        }
        return 0.0f;
    }

    // 応答 component を載せた GameObject から暗転を引く
    NS::Game::Level::ScreenFade* FindFade(NS::Obj::Scene& scene)
    {
        NS::Game::Level::ScreenFade* found = nullptr;
        scene.Objects().ForEachComponent<NS::Game::Level::ScreenFade>(
            [&found](NS::Game::Level::ScreenFade& fade) {
                if (found == nullptr)
                    found = &fade;
            });
        return found;
    }
} // namespace

TEST(ModeToggle, InitialModeIsEdit)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
}

TEST(ModeToggle, EnterPlayDeactivatesEditor)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
    EXPECT_FALSE(editor.Editor().IsActive());
    EXPECT_TRUE(scene.IsSimulationEnabled());
}

TEST(ModeToggle, EnterEditReactivatesEditor)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    EXPECT_TRUE(editor.Editor().IsActive());
    EXPECT_FALSE(scene.IsSimulationEnabled());
}

TEST(ModeToggle, RedundantEnterIsNoOp)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    editor.EnterPlay();
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
}

TEST(ModeToggle, EditorStateIsPreservedAcrossToggle)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    // 適用経路を grid 編集へ差す。照会と採番は実行時と同じ live 配線
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    editor.Editor().SetApplier(&applier);
    editor.Editor().SetFindCellObjectFn([&scene](std::int16_t x, std::int16_t y, std::int16_t z) {
        return NS::Editor::FindObjectIdAtCell(scene.Objects(), x, y, z);
    });
    editor.Editor().SetAllocateIdFn([&scene]() { return scene.Objects().AllocateObjectId(); });
    editor.Editor().PlaceUnderCursorProgrammatic(5, 0, 3);
    const auto undoSizeBefore = editor.Editor().Undo().UndoSize();
    ASSERT_GE(undoSizeBefore, 1u);
    const auto objectsBefore = scene.Objects().ObjectCount();

    editor.EnterPlay();
    editor.EnterEdit();

    EXPECT_EQ(editor.Editor().Undo().UndoSize(), undoSizeBefore);
    EXPECT_EQ(scene.Objects().ObjectCount(), objectsBefore);
}

TEST(ModeToggle, SingleFrameFlipIsComplete)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
}

TEST(ModeToggle, QuitToEditWhilePausedResetsPausedFlag)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    editor.TogglePlayPause();
    ASSERT_TRUE(scene.IsSimulationPaused());
    editor.EnterEdit();
    EXPECT_FALSE(scene.IsSimulationPaused());

    editor.EnterPlay();
    EXPECT_FALSE(scene.IsSimulationPaused());
}

TEST(ModeToggle, EditModeRebuildKeepsWorldStill)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    editor.EnterPlay();
    editor.EnterEdit();

    // 編集中の構造編集で世界が組み直っても、止めた世界は動き出さない
    NS::Obj::ObjectData player = MakePlayerObject(NS::Core::Vector3{0.0f, 2.0f, 0.0f}, NS::Core::Quaternion{});
    const std::uint32_t playerId = scene.Objects().AllocateObjectId();
    applier.ApplyObjectSnapshot(playerId, player);
    NS::Obj::ObjectData hazard = MakeTriggerHazard();
    NS::Obj::SetObjectPosition(hazard, NS::Core::Vector3{0.0f, 2.0f, 0.0f});
    const std::uint32_t hazardId = scene.Objects().AllocateObjectId();
    applier.ApplyObjectSnapshot(hazardId, hazard);

    scene.OnUpdate();

    Player* live = FindPlayer(scene.Objects());
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->Health(), 8);
}

TEST(ModeToggle, EnterPlayPlacesPlayerAtBaselinePosition)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Obj::SceneData live;
    live.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.0f, 1.0f, 1.0f}, NS::Core::Quaternion{}));
    scene.LoadFromData(std::move(live));
    NS::Obj::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{7.0f, 2.0f, -4.0f}, NS::Core::Quaternion{}));
    scene.SetPlayBaselineForTest(std::move(data));
    editor.EnterPlay();

    // プレイヤーの位置は capsule 中心のワールド座標そのもので、凍結スナップショットの値がそのまま入る
    NS::Obj::GameObject* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    EXPECT_NEAR(player->Root().Position().x, 7.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().z, -4.0f, 1e-4f);
}

TEST(ModeToggle, EnterEditRestoresPoseMovedDuringPlay)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Obj::SceneData data;
    data.objects.push_back(MakeRock(1.0f, 2.0f, 3.0f));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    NS::Obj::GameObject* rock = FindFirstPlaced(scene.Objects());
    ASSERT_NE(rock, nullptr);
    const std::uint32_t rockId = rock->Id();
    rock->Root().SetPosition(NS::Core::Vector3{50.0f, 60.0f, 70.0f});

    editor.EnterEdit();

    NS::Obj::GameObject* restored = scene.Objects().FindObject(NS::Obj::ObjectRef{rockId});
    ASSERT_NE(restored, nullptr);
    EXPECT_NEAR(restored->Root().Position().x, 1.0f, 1e-4f);
    EXPECT_NEAR(restored->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(restored->Root().Position().z, 3.0f, 1e-4f);
}

TEST(ModeToggle, EnterEditRevivesObjectDestroyedDuringPlay)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Obj::SceneData data;
    data.objects.push_back(MakeRock(1.0f, 2.0f, 3.0f));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    NS::Obj::GameObject* rock = FindFirstPlaced(scene.Objects());
    ASSERT_NE(rock, nullptr);
    const std::uint32_t rockId = rock->Id();
    scene.DestroyObject(rockId);
    ASSERT_EQ(scene.Objects().FindObject(NS::Obj::ObjectRef{rockId}), nullptr);

    editor.EnterEdit();

    NS::Obj::GameObject* revived = scene.Objects().FindObject(NS::Obj::ObjectRef{rockId});
    ASSERT_NE(revived, nullptr);
    EXPECT_NEAR(revived->Root().Position().x, 1.0f, 1e-4f);
    EXPECT_NEAR(revived->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(revived->Root().Position().z, 3.0f, 1e-4f);
}

TEST(ModeToggle, PlayInspectorEditSurvivesReturnToEdit)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Obj::SceneData data;
    NS::Obj::ObjectData rock = MakeRock(1.0f, 2.0f, 3.0f);
    rock.components.push_back(NS::Obj::MakeComponentEntry("LaunchedBody"));
    data.objects.push_back(std::move(rock));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    NS::Obj::GameObject* live = FindFirstPlaced(scene.Objects());
    ASSERT_NE(live, nullptr);
    const std::uint32_t rockId = live->Id();
    live->Root().SetPosition(NS::Core::Vector3{50.0f, 60.0f, 70.0f});
    auto* launched = live->FindComponent<NS::Game::Level::LaunchedBody>();
    ASSERT_NE(launched, nullptr);
    SetFloatField(*launched, "跳ね返り", 0.9f);
    editor.MirrorPlayEditToBaseline(*launched, "跳ね返り");

    editor.EnterEdit();

    NS::Obj::GameObject* restored = scene.Objects().FindObject(NS::Obj::ObjectRef{rockId});
    ASSERT_NE(restored, nullptr);
    EXPECT_NEAR(restored->Root().Position().x, 1.0f, 1e-4f);
    EXPECT_NEAR(restored->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(restored->Root().Position().z, 3.0f, 1e-4f);
    auto* restoredLaunched = restored->FindComponent<NS::Game::Level::LaunchedBody>();
    ASSERT_NE(restoredLaunched, nullptr);
    EXPECT_FLOAT_EQ(GetFloatField(*restoredLaunched, "跳ね返り"), 0.9f);
}

TEST(ModeToggle, EnterEditCancelsInFlightFade)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Obj::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // 編集へ戻ると進行中の暗転は破棄され、次のプレイ開始へ持ち越さない
    editor.EnterEdit();
    fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    EXPECT_FALSE(fade->IsFading());
    EXPECT_NEAR(fade->Alpha(), 0.0f, 1e-6f);
}

TEST(ModeToggle, CancelledClearDoesNotRefireAfterReenter)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Obj::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    data.objects.push_back(MakeGoal(5.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->Root().SetPosition(NS::Core::Vector3{5.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // 編集へ戻ってもう一度プレイへ。前のプレイで立ったゴールのフラグは戻っているので開始直後に再クリアしない
    editor.EnterEdit();
    editor.EnterPlay();
    fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    scene.OnUpdate();
    EXPECT_FALSE(fade->IsFading());
}
