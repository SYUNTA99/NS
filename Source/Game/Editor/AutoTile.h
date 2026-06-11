#pragma once

/// @file AutoTile.h
/// @brief 地形オートタイリング — 6 方向 neighbor の bitmask 計算 + theme 別 slice 引き + spawn marker setter
///
/// @details `ComputeNeighborMask` で 6 面 bitmask を算出し、 `LookupTextureSlice` で
/// `(theme, mask, blockId)` を `TextureArray` の slice index に変換する。 slice 引きは
/// 64 entry の縮約テーブルで mask を 8 variant に丸め、 theme 別の base slice に加算する
/// SpawnMarker は値が 1 つだけ (上書き運用) なので Command 経路ではなく直接 setter を提供する

#include <cstdint>

#include "Game/Theme/ThemeId.h"

namespace NS::Game::Level
{
    struct LevelData;
}

namespace NS::Game::Editor
{

    /// 6-neighbor bitmask。 bit 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    /// 隣接 cell に同 `blockId` の block があれば bit が立つ
    [[nodiscard]] std::uint8_t ComputeNeighborMask(const NS::Game::Level::LevelData& level,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z,
                                                   std::uint16_t blockId) noexcept;

    /// 6-neighbor mask を 64 entry テーブルで 8 variant に縮約し ThemeRegistry の base slice に加算する
    /// theme 範囲外は Grass、 mask>=64 は slice 0 にフォールバック
    [[nodiscard]] std::uint16_t LookupTextureSlice(ThemeId theme,
                                                   std::uint8_t neighborMask,
                                                   std::uint16_t blockId) noexcept;

    /// 1 spawn 限定なので Command 経路を通さない直接 setter
    void SetSpawnMarker(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

} // namespace NS::Game::Editor
