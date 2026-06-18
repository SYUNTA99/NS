#pragma once

/// @file CollisionGrid.h
/// @brief NS::Physics::CollisionGrid — 静的 collision プリミティブの一様グリッド ブロードフェーズ
///
/// @details 各プリミティブの world AABB が重なるセルへ index を登録し、 query AABB 近傍の
/// 候補 index だけを返す。 静的ジオメトリ前提で collision 再構築時に 1 度 Build する
/// 取りこぼし無し (false negative 無し) が要件、 false positive は narrow phase が弾く

#include "Framework/Math/Math.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace NS::Physics
{
    class CollisionGrid
    {
    public:
        /// world AABB 群を cellSize の一様グリッドへ投入する。 index は boxes の添字
        void Build(const std::vector<NS::Math::AABB>& boxes, float cellSize) noexcept;

        /// queryBox が重なるセルに登録された候補 index を out へ集める (昇順・重複除去済)
        void Query(const NS::Math::AABB& queryBox, std::vector<std::uint32_t>& out) const noexcept;

        /// 空かどうか (未 Build / プリミティブ無し)
        [[nodiscard]] bool IsEmpty() const noexcept;

    private:
        [[nodiscard]] static std::int64_t CellKey(int x, int y, int z) noexcept;

        float m_cellSize = 1.0f;
        std::unordered_map<std::int64_t, std::vector<std::uint32_t>> m_cells;
    };
} // namespace NS::Physics
