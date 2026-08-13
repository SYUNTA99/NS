#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/Undo/IObjectSnapshotApplier.h"
#include "Game/Level/BlockObject.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/World.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

namespace
{
    // 実 world を持たずに grid 編集を検証する適用経路。 EditorMode が読むのと同じ SceneData を直接いじる
    // ObjectSnapshotApplier の差し替え/新規/除去だけを再現する。組み直しはしない
    class RefApplier final : public EditorNs::IObjectSnapshotApplier
    {
    public:
        explicit RefApplier(SceneNs::SceneData& target) noexcept : data(target) {}

        [[nodiscard]] std::optional<SceneNs::ObjectData> CaptureObject(std::uint32_t id) const override
        {
            const std::size_t index = SceneNs::FindObjectIndexById(data, id);
            if (index == SceneNs::k_NoObjectIndex)
                return std::nullopt;
            return data.objects[index];
        }

        void ApplyObjectSnapshot(std::uint32_t id, const std::optional<SceneNs::ObjectData>& desired) override
        {
            const std::size_t index = SceneNs::FindObjectIndexById(data, id);
            if (desired)
            {
                SceneNs::ObjectData entry = *desired;
                entry.objectId = id;
                if (index != SceneNs::k_NoObjectIndex)
                    data.objects[index] = std::move(entry);
                else
                    data.objects.push_back(std::move(entry));
            }
            else if (index != SceneNs::k_NoObjectIndex)
            {
                data.objects.erase(data.objects.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

    private:
        SceneNs::SceneData& data;
    };

    // live 照会と採番を代行する配線。 実行中の scene 配線と同じ取り決め
    void WireLevel(EditorNs::EditorMode& editor, SceneNs::SceneData& lv, SceneNs::World& world)
    {
        editor.SetFindCellObjectFn([&lv](std::int16_t x, std::int16_t y, std::int16_t z) {
            const std::size_t index = EditorNs::FindObjectAtCell(lv, x, y, z);
            if (index == SceneNs::k_NoObjectIndex)
            {
                return SceneNs::k_NoObjectId;
            }
            return lv.objects[index].objectId;
        });
        editor.SetAllocateIdFn([&world]() { return world.AllocateObjectId(); });
    }
} // namespace

TEST(EditorMode, ProgrammaticPlaceAddsBlock)
{
    SceneNs::SceneData lv;
    EditorNs::EditorMode editor;
    SceneNs::World world;
    WireLevel(editor, lv, world);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.PlaceUnderCursorProgrammatic(5, 0, 3);

    ASSERT_EQ(lv.objects.size(), 1u);
    const auto idx = EditorNs::FindObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, SceneNs::k_NoObjectIndex);
    EXPECT_EQ(EditorNs::ObjectCellX(lv.objects[idx]), 5);
    EXPECT_EQ(EditorNs::ObjectCellY(lv.objects[idx]), 0);
    EXPECT_EQ(EditorNs::ObjectCellZ(lv.objects[idx]), 3);
    EXPECT_TRUE(NS::Editor::IsSolidObject(lv.objects[idx]));
}

TEST(EditorMode, ProgrammaticDeleteRemovesBlock)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(2, 0, 4));
    SceneNs::EnsureUniqueObjectIds(lv);
    EditorNs::EditorMode editor;
    SceneNs::World world;
    WireLevel(editor, lv, world);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.DeleteAtProgrammatic(2, 0, 4);

    EXPECT_TRUE(lv.objects.empty());
}

TEST(EditorMode, ProgrammaticRotateCycles)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(lv);
    EditorNs::EditorMode editor;
    SceneNs::World world;
    WireLevel(editor, lv, world);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(lv.objects[0]), 1);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(lv.objects[0]), 2);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(lv.objects[0]), 3);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(lv.objects[0]), 0);
}

TEST(EditorMode, UndoStackIntegration)
{
    SceneNs::SceneData lv;
    EditorNs::EditorMode editor;
    SceneNs::World world;
    WireLevel(editor, lv, world);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    ASSERT_EQ(lv.objects.size(), 1u);
    ASSERT_EQ(editor.Undo().UndoSize(), 1u);

    ASSERT_TRUE(editor.Undo().Undo(applier));
    EXPECT_TRUE(lv.objects.empty());
}

TEST(EditorMode, LevelDirtyFlagSetByMutation)
{
    SceneNs::SceneData lv;
    EditorNs::EditorMode editor;
    SceneNs::World world;
    WireLevel(editor, lv, world);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.ClearLevelDirty();
    EXPECT_FALSE(editor.IsLevelDirty());

    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    EXPECT_TRUE(editor.IsLevelDirty());

    editor.ClearLevelDirty();
    editor.DeleteAtProgrammatic(0, 0, 0);
    EXPECT_TRUE(editor.IsLevelDirty());
}

TEST(EditorMode, CellRotationViaProgrammaticOnExistingBlock)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(lv);
    EditorNs::EditorMode editor;
    SceneNs::World world;
    WireLevel(editor, lv, world);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(lv.objects[0]), 1);
    EXPECT_TRUE(editor.IsLevelDirty());

    ASSERT_TRUE(editor.Undo().Undo(applier));
    EXPECT_EQ(EditorNs::CellRotationStep(lv.objects[0]), 0);
}
