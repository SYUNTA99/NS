#include "Runtime/Physics/CollisionGrid.h"

#include <algorithm>

namespace NS::Physics
{
    std::int64_t CollisionGrid::CellKey(int x, int y, int z) noexcept
    {
        // 各軸 21 bit で約 ±100 万セルを pack する。 レベル範囲には十分
        const std::int64_t ux = static_cast<std::int64_t>(x) & 0x1FFFFF;
        const std::int64_t uy = static_cast<std::int64_t>(y) & 0x1FFFFF;
        const std::int64_t uz = static_cast<std::int64_t>(z) & 0x1FFFFF;

        return (ux << 42) | (uy << 21) | uz;
    }

    void CollisionGrid::Build(const std::vector<NS::Core::AABB>& boxes, float cellSize) noexcept
    {
        m_cells.clear();
        if (cellSize > 1e-4f)
        {
            m_cellSize = cellSize;
        }
        else
        {
            m_cellSize = 1.0f;
        }

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(boxes.size()); ++i)
        {
            const NS::Core::AABB& b = boxes[i];
            const int minX = static_cast<int>(std::floor((b.Center.x - b.Extents.x) / m_cellSize));
            const int minY = static_cast<int>(std::floor((b.Center.y - b.Extents.y) / m_cellSize));
            const int minZ = static_cast<int>(std::floor((b.Center.z - b.Extents.z) / m_cellSize));
            const int maxX = static_cast<int>(std::floor((b.Center.x + b.Extents.x) / m_cellSize));
            const int maxY = static_cast<int>(std::floor((b.Center.y + b.Extents.y) / m_cellSize));
            const int maxZ = static_cast<int>(std::floor((b.Center.z + b.Extents.z) / m_cellSize));
            for (int z = minZ; z <= maxZ; ++z)
            {
                for (int y = minY; y <= maxY; ++y)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        m_cells[CellKey(x, y, z)].push_back(i);
                    }
                }
            }
        }
    }

    void CollisionGrid::Query(const NS::Core::AABB& queryBox, std::vector<std::uint32_t>& out) const noexcept
    {
        out.clear();
        const int minX = static_cast<int>(std::floor((queryBox.Center.x - queryBox.Extents.x) / m_cellSize));
        const int minY = static_cast<int>(std::floor((queryBox.Center.y - queryBox.Extents.y) / m_cellSize));
        const int minZ = static_cast<int>(std::floor((queryBox.Center.z - queryBox.Extents.z) / m_cellSize));
        const int maxX = static_cast<int>(std::floor((queryBox.Center.x + queryBox.Extents.x) / m_cellSize));
        const int maxY = static_cast<int>(std::floor((queryBox.Center.y + queryBox.Extents.y) / m_cellSize));
        const int maxZ = static_cast<int>(std::floor((queryBox.Center.z + queryBox.Extents.z) / m_cellSize));

        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const auto it = m_cells.find(CellKey(x, y, z));
                    if (it == m_cells.end())
                    {
                        continue;
                    }
                    for (const std::uint32_t index : it->second)
                    {
                        out.push_back(index);
                    }
                }
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    bool CollisionGrid::IsEmpty() const noexcept
    {
        return m_cells.empty();
    }
} // namespace NS::Physics
