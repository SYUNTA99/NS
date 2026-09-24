#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/Undo/IObjectSnapshotApplier.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>

namespace EditorNs = NS::Editor;
namespace SceneNs = NS::Obj;

namespace
{
    // 実の配置物を持たずに grid 編集を検証する適用経路。シーンの JSON を直接いじる
    // ObjectSnapshotApplier の差し替え/新規/除去だけを再現する。組み直しはしない
    class RefApplier final : public EditorNs::IObjectSnapshotApplier
    {
    public:
        explicit RefApplier(nlohmann::json& target) noexcept : data(target) {}

        [[nodiscard]] std::optional<nlohmann::json> CaptureObject(std::uint32_t id) const override
        {
            const std::size_t index = SceneNs::FindObjectIndexById(data, id);
            if (index == SceneNs::k_NoObjectIndex)
                return std::nullopt;
            return std::make_optional<nlohmann::json>(SceneNs::SceneJsonObjects(data)[index]);
        }

        void ApplyObjectSnapshot(std::uint32_t id, const std::optional<nlohmann::json>& desired) override
        {
            const std::size_t index = SceneNs::FindObjectIndexById(data, id);
            nlohmann::json& objects = SceneNs::SceneJsonObjects(data);
            if (desired)
            {
                nlohmann::json entry = *desired;
                SceneNs::SetObjectJsonId(entry, id);
                if (index != SceneNs::k_NoObjectIndex)
                    objects[index] = std::move(entry);
                else
                    objects.push_back(std::move(entry));
            }
            else if (index != SceneNs::k_NoObjectIndex)
            {
                objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

    private:
        nlohmann::json& data;
    };

    // live 照会と採番を代行する配線。実行中の scene 配線と同じ取り決め
    void WireLevel(EditorNs::EditorMode& editor, nlohmann::json& lv, SceneNs::ObjectList& objects)
    {
        editor.SetFindCellObjectFn([&lv](std::int16_t x, std::int16_t y, std::int16_t z) {
            const std::size_t index = EditorNs::FindObjectAtCell(lv, x, y, z);
            if (index == SceneNs::k_NoObjectIndex)
            {
                return SceneNs::k_NoObjectId;
            }
            return SceneNs::ObjectJsonId(SceneNs::SceneJsonObjects(lv)[index]);
        });
        editor.SetAllocateIdFn([&objects]() { return objects.AllocateObjectId(); });
    }
} // namespace

TEST(EditorMode, ProgrammaticPlaceAddsBlock)
{
    nlohmann::json lv = SceneNs::MakeSceneJson();
    EditorNs::EditorMode editor;
    SceneNs::ObjectList objects;
    WireLevel(editor, lv, objects);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.PlaceUnderCursorProgrammatic(5, 0, 3);

    ASSERT_EQ(SceneNs::SceneJsonObjects(lv).size(), 1u);
    const std::size_t idx = EditorNs::FindObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, SceneNs::k_NoObjectIndex);
    EXPECT_EQ(EditorNs::ObjectCellX(SceneNs::SceneJsonObjects(lv)[idx]), 5);
    EXPECT_EQ(EditorNs::ObjectCellY(SceneNs::SceneJsonObjects(lv)[idx]), 0);
    EXPECT_EQ(EditorNs::ObjectCellZ(SceneNs::SceneJsonObjects(lv)[idx]), 3);
    EXPECT_TRUE(NS::Editor::IsSolidObject(SceneNs::SceneJsonObjects(lv)[idx]));
}

TEST(EditorMode, ProgrammaticDeleteRemovesBlock)
{
    nlohmann::json lv = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(lv).push_back(NS::Editor::MakeCellObject(2, 0, 4));
    SceneNs::EnsureUniqueObjectIds(lv);
    EditorNs::EditorMode editor;
    SceneNs::ObjectList objects;
    WireLevel(editor, lv, objects);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.DeleteAtProgrammatic(2, 0, 4);

    EXPECT_TRUE(SceneNs::SceneJsonObjects(lv).empty());
}

TEST(EditorMode, ProgrammaticRotateCycles)
{
    nlohmann::json lv = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(lv).push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(lv);
    EditorNs::EditorMode editor;
    SceneNs::ObjectList objects;
    WireLevel(editor, lv, objects);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(SceneNs::SceneJsonObjects(lv)[0]), 1);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(SceneNs::SceneJsonObjects(lv)[0]), 2);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(SceneNs::SceneJsonObjects(lv)[0]), 3);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(SceneNs::SceneJsonObjects(lv)[0]), 0);
}

TEST(EditorMode, UndoStackIntegration)
{
    nlohmann::json lv = SceneNs::MakeSceneJson();
    EditorNs::EditorMode editor;
    SceneNs::ObjectList objects;
    WireLevel(editor, lv, objects);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    ASSERT_EQ(SceneNs::SceneJsonObjects(lv).size(), 1u);
    ASSERT_EQ(editor.Undo().UndoSize(), 1u);

    ASSERT_TRUE(editor.Undo().Undo(applier));
    EXPECT_TRUE(SceneNs::SceneJsonObjects(lv).empty());
}

TEST(EditorMode, LevelDirtyFlagSetByMutation)
{
    nlohmann::json lv = SceneNs::MakeSceneJson();
    EditorNs::EditorMode editor;
    SceneNs::ObjectList objects;
    WireLevel(editor, lv, objects);
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
    nlohmann::json lv = SceneNs::MakeSceneJson();
    SceneNs::SceneJsonObjects(lv).push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(lv);
    EditorNs::EditorMode editor;
    SceneNs::ObjectList objects;
    WireLevel(editor, lv, objects);
    RefApplier applier(lv);
    editor.SetApplier(&applier);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(EditorNs::CellRotationStep(SceneNs::SceneJsonObjects(lv)[0]), 1);
    EXPECT_TRUE(editor.IsLevelDirty());

    ASSERT_TRUE(editor.Undo().Undo(applier));
    EXPECT_EQ(EditorNs::CellRotationStep(SceneNs::SceneJsonObjects(lv)[0]), 0);
}
