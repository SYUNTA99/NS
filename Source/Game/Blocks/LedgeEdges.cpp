#include "Game/Blocks/LedgeEdges.h"

#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelData.h"

#include <cstddef>
#include <cstdint>

namespace NS::Game::Blocks
{
    namespace
    {
        // cell が grid 固形か。 範囲外 / 非固形は false。 固形性のみ見て見た目は問わない
        [[nodiscard]] bool IsSolidCell(const NS::Game::Level::LevelData& level,
                                       std::int16_t x,
                                       std::int16_t y,
                                       std::int16_t z) noexcept
        {
            const std::size_t idx = NS::Game::Level::FindGridObjectAtCell(level, x, y, z);
            if (idx == NS::Game::Level::kNoObjectIndex)
                return false;
            return IsGridSolidObject(level.objects[idx]);
        }
    } // namespace

    std::vector<LedgeEdge> ComputeTopLedgeEdges(const NS::Game::Level::LevelData& level)
    {
        std::vector<LedgeEdge> edges;
        for (const NS::Game::Level::ObjectInstance& object : level.objects)
        {
            if (!IsGridSolidObject(object))
                continue;
            const std::int16_t x = NS::Game::Level::ObjectCellX(object);
            const std::int16_t y = NS::Game::Level::ObjectCellY(object);
            const std::int16_t z = NS::Game::Level::ObjectCellZ(object);

            // 真上が固形なら天面が塞がれ立てないので縁を出さない
            if (IsSolidCell(level, x, static_cast<std::int16_t>(y + 1), z))
                continue;

            const float cx = static_cast<float>(x);
            const float cz = static_cast<float>(z);
            const float top = static_cast<float>(y) + 0.5f;
            const float xMin = cx - 0.5f;
            const float xMax = cx + 0.5f;
            const float zMin = cz - 0.5f;
            const float zMax = cz + 0.5f;

            // 側方の隣が固形でなければ、 その天面の縁辺は踏み外せる縁。 outward は空セル側へ向ける
            if (!IsSolidCell(level, static_cast<std::int16_t>(x + 1), y, z))
                edges.push_back(LedgeEdge{NS::Math::Vector3{xMax, top, zMin},
                                          NS::Math::Vector3{xMax, top, zMax},
                                          NS::Math::Vector3{1.0f, 0.0f, 0.0f}});
            if (!IsSolidCell(level, static_cast<std::int16_t>(x - 1), y, z))
                edges.push_back(LedgeEdge{NS::Math::Vector3{xMin, top, zMin},
                                          NS::Math::Vector3{xMin, top, zMax},
                                          NS::Math::Vector3{-1.0f, 0.0f, 0.0f}});
            if (!IsSolidCell(level, x, y, static_cast<std::int16_t>(z + 1)))
                edges.push_back(LedgeEdge{NS::Math::Vector3{xMin, top, zMax},
                                          NS::Math::Vector3{xMax, top, zMax},
                                          NS::Math::Vector3{0.0f, 0.0f, 1.0f}});
            if (!IsSolidCell(level, x, y, static_cast<std::int16_t>(z - 1)))
                edges.push_back(LedgeEdge{NS::Math::Vector3{xMin, top, zMin},
                                          NS::Math::Vector3{xMax, top, zMin},
                                          NS::Math::Vector3{0.0f, 0.0f, -1.0f}});
        }
        return edges;
    }
} // namespace NS::Game::Blocks
