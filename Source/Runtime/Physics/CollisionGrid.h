#pragma once

#include "Runtime/Math/Math.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace NS::Physics
{
    /// @brief 静的 collision プリミティブの一様グリッド broadphase
    /// @details 各プリミティブの world AABB が重なるセルへ index を登録する
    /// query 時はその AABB 近傍のセルに載った候補 index だけを返す
    /// 静的ジオメトリ前提で、collision 再構築時に 1 度 Build する
    /// 候補の取りこぼしは許さない。余分な候補は narrow phase が弾く
    class CollisionGrid
    {
    public:
        /// world AABB 群を cellSize 刻みの一様グリッドへ登録する。index は boxes の添字
        void Build(const std::vector<NS::Math::AABB>& boxes, float cellSize) noexcept;

        /// queryBox が重なるセルに登録された候補 index を昇順・重複なしで out へ集める
        void Query(const NS::Math::AABB& queryBox, std::vector<std::uint32_t>& out) const noexcept;

        /// 空かどうか。未 Build かプリミティブ無しなら空
        [[nodiscard]] bool IsEmpty() const noexcept;

    private:
        [[nodiscard]] static std::int64_t CellKey(int x, int y, int z) noexcept;

        float m_cellSize = 1.0f;
        std::unordered_map<std::int64_t, std::vector<std::uint32_t>> m_cells;
    };
} // namespace NS::Physics
