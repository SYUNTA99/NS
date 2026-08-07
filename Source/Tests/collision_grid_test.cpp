#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Physics/CollisionGrid.h>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Physics::CollisionGrid;

    AABB MakeBox(const Vector3& center, const Vector3& extents)
    {
        AABB b;
        b.Center = center;
        b.Extents = extents;
        return b;
    }

    bool Contains(const std::vector<std::uint32_t>& v, std::uint32_t x)
    {
        return std::find(v.begin(), v.end(), x) != v.end();
    }
} // namespace

// 離れた 3 箱のうち near 箱の近傍 query は その index だけを返す
TEST(CollisionGridTest, QueryReturnsOnlyNearbyCandidates)
{
    std::vector<AABB> boxes;
    boxes.push_back(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    boxes.push_back(MakeBox({50.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    boxes.push_back(MakeBox({0.0f, 0.0f, 50.0f}, {0.5f, 0.5f, 0.5f}));

    CollisionGrid grid;
    grid.Build(boxes, 2.0f);

    std::vector<std::uint32_t> out;
    grid.Query(MakeBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}), out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 0u);
}

// 空グリッドへの query は空を返す
TEST(CollisionGridTest, EmptyGridQueryReturnsEmpty)
{
    CollisionGrid grid;
    EXPECT_TRUE(grid.IsEmpty());
    std::vector<std::uint32_t> out;
    grid.Query(MakeBox({0.0f, 0.0f, 0.0f}, {5.0f, 5.0f, 5.0f}), out);
    EXPECT_TRUE(out.empty());
}

// 全体を覆う query は全 index を重複なく返す
TEST(CollisionGridTest, QueryCoveringAllReturnsEveryIndexOnce)
{
    std::vector<AABB> boxes;
    boxes.reserve(5);
    for (int i = 0; i < 5; ++i)
        boxes.push_back(MakeBox({static_cast<float>(i) * 4.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));

    CollisionGrid grid;
    grid.Build(boxes, 2.0f);

    std::vector<std::uint32_t> out;
    grid.Query(MakeBox({8.0f, 0.0f, 0.0f}, {20.0f, 5.0f, 5.0f}), out);

    ASSERT_EQ(out.size(), 5u);
    for (std::uint32_t i = 0; i < 5u; ++i)
        EXPECT_TRUE(Contains(out, i));
}

// 複数セルにまたがる箱は query 結果で 1 度だけ現れる (dedup)
TEST(CollisionGridTest, LargeBoxSpanningCellsDeduped)
{
    std::vector<AABB> boxes;
    boxes.push_back(MakeBox({0.0f, 0.0f, 0.0f}, {3.0f, 3.0f, 3.0f}));

    CollisionGrid grid;
    grid.Build(boxes, 2.0f);

    std::vector<std::uint32_t> out;
    grid.Query(MakeBox({0.0f, 0.0f, 0.0f}, {3.0f, 3.0f, 3.0f}), out);

    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 0u);
}
