#pragma once

/// @file AutoTile.h
/// @brief 地形オートタイリング (最小実装) — 6 方向 neighbor の bitmask 計算と spawn marker setter。
///
/// @details bitmask のみ算出する。 texture index 引きは今後本格対応する想定で、
/// 今回は「隣接 cell に同 blockId がある」 という情報を返すだけ。
/// SpawnMarker は値が 1 つだけ (上書き運用) なので Command 経路ではなく直接 setter を提供する。

#include <cstdint>

namespace NS::Game::Level
{
    struct LevelData;
}

namespace NS::Game::Editor
{

    /// 6-neighbor bitmask。 bit 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z。
    /// 隣接 cell に同 `blockId` の block があれば bit が立つ。
    [[nodiscard]] std::uint8_t ComputeNeighborMask(const NS::Game::Level::LevelData& level,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z,
                                                   std::uint16_t blockId) noexcept;

    /// 1 spawn 限定なので Command 経路を通さない直接 setter。
    void SetSpawnMarker(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

} // namespace NS::Game::Editor
