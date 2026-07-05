#include "Editor/Undo/AddObjectCommand.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::GameCore::Level;

namespace
{
    LevelNs::ObjectInstance MakeFree(float x, float y, float z)
    {
        LevelNs::ObjectInstance o{};
        o.positionX = x;
        o.positionY = y;
        o.positionZ = z;
        return o; // flags 0 のまま = 非 gridAligned
    }
} // namespace

TEST(AddObjectCommandTest, DoAppendsFreeObjectAndId)
{
    LevelNs::LevelData lv;

    EditorNs::AddObjectCommand cmd(MakeFree(1.0f, 2.0f, 3.0f));
    cmd.Do(lv);

    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_NE(lv.objects[0].objectId, LevelNs::kNoObjectId);
    EXPECT_EQ(lv.nextObjectId, lv.objects[0].objectId + 1);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 1.0f);
    EXPECT_EQ(lv.objects[0].flags & LevelNs::kObjectFlagGridAligned, 0);
}

TEST(AddObjectCommandTest, UndoRemovesOnlyTheAddedObject)
{
    LevelNs::LevelData lv;
    // 既存の別 id 要素を 1 つ置き、 Undo が追加分だけを消すことを確かめる
    lv.objects.push_back(MakeFree(9.0f, 0.0f, 0.0f));
    lv.objects[0].objectId = 99;
    lv.nextObjectId = 100;

    EditorNs::AddObjectCommand cmd(MakeFree(1.0f, 1.0f, 1.0f));
    cmd.Do(lv);
    ASSERT_EQ(lv.objects.size(), 2u);

    cmd.Undo(lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(lv.objects[0].objectId, 99u);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 9.0f);
}

TEST(AddObjectCommandTest, EstimatedBytesCountsComponentHeap)
{
    // components を持つ object はその heap 分だけ概算が増える。 sizeof のみだと undo の cap が取りこぼす
    LevelNs::ObjectInstance withComponents = MakeFree(0.0f, 0.0f, 0.0f);
    LevelNs::ComponentData mesh;
    mesh.typeName = "MeshRendererComponent";
    mesh.fields.push_back(LevelNs::FieldValue{"Mesh", std::string("cube")});
    withComponents.components.push_back(std::move(mesh));

    EditorNs::AddObjectCommand bareCmd(MakeFree(0.0f, 0.0f, 0.0f));
    EditorNs::AddObjectCommand richCmd(withComponents);

    EXPECT_GE(bareCmd.EstimatedBytes(), sizeof(EditorNs::AddObjectCommand));
    EXPECT_GT(richCmd.EstimatedBytes(), bareCmd.EstimatedBytes());
}

TEST(AddObjectCommandTest, RedoReusesSameIdWithoutBumpingNext)
{
    LevelNs::LevelData lv;
    lv.nextObjectId = 7;

    EditorNs::AddObjectCommand cmd(MakeFree(0.0f, 0.0f, 0.0f));
    cmd.Do(lv); // id 7 を採番
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(lv.objects[0].objectId, 7u);
    EXPECT_EQ(lv.nextObjectId, 8u);

    cmd.Undo(lv);
    cmd.Do(lv); // redo: 同じ id 7 を再利用しカウンタは増やさない
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(lv.objects[0].objectId, 7u);
    EXPECT_EQ(lv.nextObjectId, 8u);
}
