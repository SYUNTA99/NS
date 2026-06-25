#include "Game/Level/LevelData.h"
#include "Editor/Undo/AddObjectCommand.h"
#include "Game/Level/EditTarget.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

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
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};

    EditorNs::AddObjectCommand cmd(MakeFree(1.0f, 2.0f, 3.0f));
    cmd.Do(t);

    ASSERT_EQ(lv.objects.size(), 1u);
    ASSERT_EQ(ids.size(), lv.objects.size());
    EXPECT_EQ(ids[0], 0u);
    EXPECT_EQ(next, 1u);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 1.0f);
    EXPECT_EQ(lv.objects[0].flags & LevelNs::kObjectFlagGridAligned, 0);
}

TEST(AddObjectCommandTest, UndoRemovesOnlyTheAddedObject)
{
    LevelNs::LevelData lv;
    // 既存の別 id 要素を 1 つ置き、 Undo が末尾 (追加分) だけを消すことを確かめる
    lv.objects.push_back(MakeFree(9.0f, 0.0f, 0.0f));
    std::vector<std::uint32_t> ids{99u};
    std::uint32_t next = 100;
    LevelNs::EditTarget t{lv, ids, next};

    EditorNs::AddObjectCommand cmd(MakeFree(1.0f, 1.0f, 1.0f));
    cmd.Do(t);
    ASSERT_EQ(lv.objects.size(), 2u);

    cmd.Undo(t);
    ASSERT_EQ(lv.objects.size(), 1u);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 99u);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 9.0f);
}

TEST(AddObjectCommandTest, RedoReusesSameIdWithoutBumpingNext)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 7;
    LevelNs::EditTarget t{lv, ids, next};

    EditorNs::AddObjectCommand cmd(MakeFree(0.0f, 0.0f, 0.0f));
    cmd.Do(t); // id 7 を採番
    EXPECT_EQ(ids[0], 7u);
    EXPECT_EQ(next, 8u);

    cmd.Undo(t);
    cmd.Do(t); // redo: 同じ id 7 を再利用し next は増やさない
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 7u);
    EXPECT_EQ(next, 8u);
}
