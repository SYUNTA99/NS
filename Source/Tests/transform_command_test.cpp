#include "Editor/Undo/TransformCommand.h"
#include "Framework/Scene/SceneData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;
namespace SceneNs = NS::Scene;

namespace
{
    SceneNs::ObjectData MakeFree(float x, float y, float z)
    {
        SceneNs::ObjectData o;
        o.positionX = x;
        o.positionY = y;
        o.positionZ = z;
        return o;
    }
} // namespace

TEST(TransformCommandTest, DoReplacesWithAfterUndoRestoresBefore)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    lv.objects[0].objectId = 7;

    const auto before = lv.objects[0];
    auto after = before;
    after.positionX = 5.0f;

    EditorNs::TransformCommand cmd(7, before, after);
    cmd.Do(lv);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 5.0f);
    cmd.Undo(lv);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 0.0f);
}

TEST(TransformCommandTest, MaterialAndPositionRoundTrip)
{
    // 材質のドロップ適用は TransformCommand で undo する。 位置以外の欄も full-instance 差替で往復する
    SceneNs::SceneData lv;
    SceneNs::ObjectData object = MakeFree(2, 0, 2);
    object.materialIndex = -1;
    object.objectId = 3;
    lv.objects.push_back(object);

    auto after = object;
    after.materialIndex = 4;
    after.positionX = 6.0f;

    EditorNs::TransformCommand cmd(3, object, after);
    cmd.Do(lv);
    EXPECT_EQ(lv.objects[0].materialIndex, 4);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 6.0f);
    cmd.Undo(lv);
    EXPECT_EQ(lv.objects[0].materialIndex, -1);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 2.0f);
}

TEST(TransformCommandTest, ResolvesByIdAfterIndexShift)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    lv.objects.push_back(MakeFree(1, 1, 1));
    lv.objects[0].objectId = 10;
    lv.objects[1].objectId = 11;

    const auto before = lv.objects[1];
    auto after = before;
    after.positionY = 9.0f;
    EditorNs::TransformCommand cmd(11, before, after);

    // 先頭を削って添字を 1 つずらす。 id 11 は index 0 へ移る
    lv.objects.erase(lv.objects.begin());

    cmd.Do(lv);
    const std::size_t idx = SceneNs::FindObjectIndexById(lv, 11);
    ASSERT_NE(idx, SceneNs::kNoObjectIndex);
    EXPECT_FLOAT_EQ(lv.objects[idx].positionY, 9.0f);
}

TEST(TransformCommandTest, MissingIdIsNoOp)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    lv.objects[0].objectId = 1;

    const auto crc = lv.ComputeCrc32();
    EditorNs::TransformCommand cmd(999, lv.objects[0], MakeFree(5, 5, 5));
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), crc);
}
