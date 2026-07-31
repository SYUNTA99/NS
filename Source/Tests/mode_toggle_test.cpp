#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/LevelEditorController.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Game/Level/ScreenFadeComponent.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>
#include <utility>

/// Application に依存しない Scene + LevelEditorController でモード切替と Undo 履歴の保持を確かめる
/// Setup / OnStart は Application::Get() が要るので呼ばない。player 等は nullptr のまま
/// 世界を回す / 止める / 一時停止の実体はシーンのスイッチで、controller はそれを操作する

namespace
{
    // 接触クリアの印だけを持つゴールを組む
    NS::Object::ObjectData MakeGoal(float x, float y, float z)
    {
        NS::Object::ObjectData object;
        NS::Object::SetObjectPosition(object, NS::Math::Vector3{x, y, z});
        object.components.push_back(NS::Object::MakeComponentEntry("GoalComponent"));
        return object;
    }

    // プレイヤーと同じ場所に置くトリガの hazard 箱を組む
    NS::Object::ObjectData MakeTriggerHazard()
    {
        NS::Object::ObjectData object;
        nlohmann::json box = NS::Object::MakeComponentEntry("BoxColliderComponent");
        NS::Object::SetField(box, "Is Trigger", true);
        object.components = nlohmann::json::array({std::move(box), NS::Object::MakeComponentEntry("HazardComponent")});
        return object;
    }

    // 応答部品の器に載った暗転を引く
    NS::Game::Level::ScreenFadeComponent* FindFade(NS::Object::Scene& scene)
    {
        NS::Game::Level::ScreenFadeComponent* found = nullptr;
        scene.World().ForEachComponent<NS::Game::Level::ScreenFadeComponent>(
            [&found](NS::Game::Level::ScreenFadeComponent& fade) {
                if (found == nullptr)
                    found = &fade;
            });
        return found;
    }
} // namespace

TEST(ModeToggle, InitialModeIsEdit)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
}

TEST(ModeToggle, EnterPlayDeactivatesEditor)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
    EXPECT_FALSE(editor.Editor().IsActive());
    EXPECT_TRUE(scene.IsSimulationEnabled());
}

TEST(ModeToggle, EnterEditReactivatesEditor)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    EXPECT_TRUE(editor.Editor().IsActive());
    EXPECT_FALSE(scene.IsSimulationEnabled());
}

TEST(ModeToggle, RedundantEnterIsNoOp)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    editor.EnterPlay();
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
}

TEST(ModeToggle, EditorStateIsPreservedAcrossToggle)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    // 適用口を grid 編集へ差す。 照会と採番は実行時と同じ live 配線
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    editor.Editor().SetApplier(&applier);
    editor.Editor().SetFindCellObjectFn([&scene](std::int16_t x, std::int16_t y, std::int16_t z) {
        return NS::Editor::FindObjectIdAtCell(scene.World(), x, y, z);
    });
    editor.Editor().SetAllocateIdFn([&scene]() { return scene.World().AllocateObjectId(); });
    editor.Editor().PlaceUnderCursorProgrammatic(5, 0, 3);
    const auto undoSizeBefore = editor.Editor().Undo().UndoSize();
    ASSERT_GE(undoSizeBefore, 1u);
    const auto objectsBefore = scene.World().ObjectCount();

    editor.EnterPlay();
    editor.EnterEdit();

    EXPECT_EQ(editor.Editor().Undo().UndoSize(), undoSizeBefore);
    EXPECT_EQ(scene.World().ObjectCount(), objectsBefore);
}

TEST(ModeToggle, SingleFrameFlipIsComplete)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
}

TEST(ModeToggle, QuitToEditWhilePausedResetsPausedFlag)
{
    NS::Object::Scene scene;
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
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    editor.EnterPlay();
    editor.EnterEdit();

    // 編集中の構造編集で世界が組み直っても、 止めた世界は動き出さない
    NS::Object::ObjectData player = MakePlayerObject(NS::Math::Vector3{0.0f, 2.0f, 0.0f}, NS::Math::Quaternion{});
    const std::uint32_t playerId = scene.World().AllocateObjectId();
    applier.ApplyObjectSnapshot(playerId, player);
    NS::Object::ObjectData hazard = MakeTriggerHazard();
    NS::Object::SetObjectPosition(hazard, NS::Math::Vector3{0.0f, 2.0f, 0.0f});
    const std::uint32_t hazardId = scene.World().AllocateObjectId();
    applier.ApplyObjectSnapshot(hazardId, hazard);

    scene.OnUpdate();

    Player* live = FindPlayer(scene.World());
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->Health(), 8);
}

TEST(ModeToggle, EnterPlayPlacesPlayerAtBaselinePosition)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    NS::Object::SceneData live;
    live.objects.push_back(MakePlayerObject(NS::Math::Vector3{1.0f, 1.0f, 1.0f}, NS::Math::Quaternion{}));
    scene.LoadFromData(std::move(live));
    NS::Object::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{7.0f, 2.0f, -4.0f}, NS::Math::Quaternion{}));
    scene.SetPlayBaselineForTest(std::move(data));
    editor.EnterPlay();

    // プレイヤーの位置は capsule 中心のワールド座標そのもので、凍結スナップショットの値がそのまま入る
    NS::Object::GameObject* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    EXPECT_NEAR(player->Root().Position().x, 7.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().y, 2.0f, 1e-4f);
    EXPECT_NEAR(player->Root().Position().z, -4.0f, 1e-4f);
}

TEST(ModeToggle, EnterEditCancelsInFlightFade)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    NS::Object::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeGoal(0.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // 編集へ戻ると進行中の暗転は破棄され、 次のプレイ開始へ持ち越さない
    editor.EnterEdit();
    EXPECT_FALSE(fade->IsFading());
    EXPECT_NEAR(fade->Alpha(), 0.0f, 1e-6f);
}

TEST(ModeToggle, CancelledClearDoesNotRefireAfterReenter)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);
    NS::Object::SceneData data;
    data.objects.push_back(MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    data.objects.push_back(MakeGoal(5.0f, 0.0f, 0.0f));
    scene.LoadFromData(std::move(data));
    editor.EnterPlay();

    Player* player = FindPlayer(scene.World());
    ASSERT_NE(player, nullptr);
    player->Root().SetPosition(NS::Math::Vector3{5.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    auto* fade = FindFade(scene);
    ASSERT_NE(fade, nullptr);
    ASSERT_TRUE(fade->IsFading());

    // 編集へ戻ってもう一度プレイへ。 前のプレイで立ったゴールの旗は戻っているので開始直後に再クリアしない
    editor.EnterEdit();
    editor.EnterPlay();
    scene.OnUpdate();
    EXPECT_FALSE(fade->IsFading());
}
