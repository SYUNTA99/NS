#include "Editor/Undo/IObjectSnapshotApplier.h"
#include "Editor/Undo/ObjectSnapshotCommand.h"
#include "Editor/Undo/UndoStack.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace EditorNs = NS::Editor;
namespace SceneNs = NS::Object;

namespace
{
    // 実 world を持たずに ObjectSnapshotCommand の往復だけを検証する適用口
    // ObjectSnapshotApplier の芯 (組み直し抜き) と同じ差し替え/新規/除去を SceneData 上で行う
    class FakeApplier final : public EditorNs::IObjectSnapshotApplier
    {
    public:
        SceneNs::SceneData data;

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
    };

    [[nodiscard]] SceneNs::ObjectData MakeObjectAt(std::uint32_t id, float x)
    {
        SceneNs::ObjectData obj;
        SceneNs::SetObjectPosition(obj, NS::Math::Vector3{x, 0.0f, 0.0f});
        obj.objectId = id;
        return obj;
    }

    [[nodiscard]] std::unique_ptr<EditorNs::ObjectSnapshotCommand> AddCommand(std::uint32_t id, float x)
    {
        return std::make_unique<EditorNs::ObjectSnapshotCommand>(id, std::nullopt, MakeObjectAt(id, x));
    }
} // namespace

TEST(UndoStackTest, EmptyStackUndoRedoReturnFalse)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;
    EXPECT_FALSE(stack.Undo(applier));
    EXPECT_FALSE(stack.Redo(applier));
}

TEST(UndoStackTest, PushExecutesDoAndStoresInUndoStack)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;
    stack.Push(AddCommand(1, 0.0f), applier);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(applier.data.objects.size(), 1u);
}

TEST(UndoStackTest, UndoRedoRoundTripPreservesState)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;

    stack.Push(AddCommand(1, 0.0f), applier);
    ASSERT_EQ(applier.data.objects.size(), 1u);

    EXPECT_TRUE(stack.Undo(applier));
    EXPECT_TRUE(applier.data.objects.empty());
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 1u);

    EXPECT_TRUE(stack.Redo(applier));
    EXPECT_EQ(applier.data.objects.size(), 1u);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, RecordStoresWithoutApplying)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;

    // 追加は先に適用しておき、 Record は Do を呼ばずに履歴だけ積む
    applier.ApplyObjectSnapshot(1, MakeObjectAt(1, 0.0f));
    SceneNs::ObjectData before = *applier.CaptureObject(1);
    SceneNs::ObjectData after = MakeObjectAt(1, 5.0f);
    applier.ApplyObjectSnapshot(1, after);

    stack.Record(std::make_unique<EditorNs::ObjectSnapshotCommand>(1, before, after));
    EXPECT_EQ(stack.UndoSize(), 1u);
    // Record は適用しないので live は Record 前のまま (5.0)
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(applier.data.objects[0]).x, 5.0f);

    // Undo で before へ戻る
    ASSERT_TRUE(stack.Undo(applier));
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(applier.data.objects[0]).x, 0.0f);
}

TEST(UndoStackTest, MaxOpsCapPopsOldest)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;
    for (std::size_t i = 0; i < EditorNs::UndoStack::k_MaxOps + 5; ++i)
    {
        stack.Push(AddCommand(static_cast<std::uint32_t>(i + 1), static_cast<float>(i)), applier);
    }
    EXPECT_EQ(stack.UndoSize(), EditorNs::UndoStack::k_MaxOps);
}

TEST(UndoStackTest, PushAfterUndoClearsRedoStack)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;
    stack.Push(AddCommand(1, 0.0f), applier);
    stack.Undo(applier);
    EXPECT_EQ(stack.RedoSize(), 1u);

    stack.Push(AddCommand(2, 1.0f), applier);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST(UndoStackTest, ClearEmptiesBothStacks)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;
    stack.Push(AddCommand(1, 0.0f), applier);
    stack.Push(AddCommand(2, 1.0f), applier);
    stack.Undo(applier);

    stack.Clear();
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(stack.EstimatedBytes(), 0u);
}

TEST(UndoStackTest, InterleavedAddAndTransformUndoInLifoOrder)
{
    EditorNs::UndoStack stack;
    FakeApplier applier;

    // 追加 (before 無 / after 有)
    stack.Push(AddCommand(1, 0.0f), applier);
    ASSERT_EQ(applier.data.objects.size(), 1u);
    const std::uint32_t id0 = applier.data.objects[0].objectId;
    ASSERT_NE(id0, SceneNs::k_NoObjectId);

    // 同じ object を移動する変形 (before / after 両方)
    SceneNs::ObjectData before = applier.data.objects[0];
    SceneNs::ObjectData after = MakeObjectAt(id0, 9.0f);
    stack.Push(std::make_unique<EditorNs::ObjectSnapshotCommand>(id0, before, after), applier);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(applier.data.objects[0]).x, 9.0f);

    // LIFO: 先に変形を戻すと元位置へ
    ASSERT_TRUE(stack.Undo(applier));
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(applier.data.objects[0]).x, 0.0f);

    // 次に追加を戻すと空になる
    ASSERT_TRUE(stack.Undo(applier));
    EXPECT_TRUE(applier.data.objects.empty());

    // redo 2 回で最終状態へ
    ASSERT_TRUE(stack.Redo(applier));
    ASSERT_TRUE(stack.Redo(applier));
    ASSERT_EQ(applier.data.objects.size(), 1u);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(applier.data.objects[0]).x, 9.0f);
}
