#include "GameCore/Blocks/LedgeEdges.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::GameCore::Level;
namespace BlocksNs = NS::GameCore::Blocks;

TEST(LedgeEdgesTest, SingleBlockHasFourTopEdges)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    const auto edges = BlocksNs::ComputeTopLedgeEdges(lv);
    ASSERT_EQ(edges.size(), 4u);
    for (const auto& e : edges)
    {
        EXPECT_FLOAT_EQ(e.a.y, 0.5f);
        EXPECT_FLOAT_EQ(e.b.y, 0.5f);
    }
}

TEST(LedgeEdgesTest, TwoInRowShareNoInteriorEdge)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    lv.objects.push_back(LevelNs::MakeCellObject(1, 0, 0, 0));
    const auto edges = BlocksNs::ComputeTopLedgeEdges(lv);
    // 各セル 4 縁から共有面の 2 縁を引いた外周のみ
    EXPECT_EQ(edges.size(), 6u);
    // 共有面 x=0.5 を Z 方向に走る縁辺が無いこと
    for (const auto& e : edges)
    {
        const bool onSharedFace = (e.a.x == 0.5f && e.b.x == 0.5f);
        EXPECT_FALSE(onSharedFace);
    }
}

TEST(LedgeEdgesTest, CoveredTopHasNoEdges)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    lv.objects.push_back(LevelNs::MakeCellObject(0, 1, 0, 0)); // 真上に固形を載せ天面を塞ぐ
    const auto edges = BlocksNs::ComputeTopLedgeEdges(lv);
    // 下のセルは天面が塞がれ縁ゼロ。 上のセルだけが 4 縁を出す
    ASSERT_EQ(edges.size(), 4u);
    for (const auto& e : edges)
        EXPECT_FLOAT_EQ(e.a.y, 1.5f);
}

TEST(LedgeEdgesTest, SolidNeighborWithDifferentMaterialStillConnects)
{
    LevelNs::LevelData lv;
    LevelNs::ObjectInstance a = LevelNs::MakeCellObject(0, 0, 0, 0);
    LevelNs::ObjectInstance b = LevelNs::MakeCellObject(1, 0, 0, 0);
    b.materialIndex = 7; // 見た目は違うが固形性は同じ
    lv.objects.push_back(a);
    lv.objects.push_back(b);
    const auto edges = BlocksNs::ComputeTopLedgeEdges(lv);
    // 固形性で連結するので共有面に縁は出ず外周 6 縁のまま
    EXPECT_EQ(edges.size(), 6u);
}
