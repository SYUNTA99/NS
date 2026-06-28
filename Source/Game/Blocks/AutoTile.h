#pragma once

/// @file AutoTile.h
/// @brief 地形オートタイリング — 6 方向 neighbor の bitmask 計算 + theme 別 slice 引き
///
/// @details `ComputeNeighborMask` で 6 面 bitmask を算出し、 `LookupTextureSlice` で
/// `(theme, mask)` を `TextureArray` の slice index に変換する。 slice 引きは
/// 64 entry の縮約テーブルで mask を 8 variant に丸め、 theme 別の base slice に加算する
/// 連結判定はメッシュ参照とマテリアルの同一性で行う

#include <cstdint>

#include "Game/Theme/ThemeId.h"

namespace NS::Game::Level
{
    struct LevelData;
}

namespace NS::Game::Blocks
{

    /// 6-neighbor bitmask。 bit 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    /// 中心 cell の object とメッシュ参照・マテリアルが同一な隣接があれば bit が立つ
    /// 中心 cell に grid object が無ければ 0
    [[nodiscard]] std::uint8_t ComputeNeighborMask(const NS::Game::Level::LevelData& level,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    /// 6-neighbor mask を 64 entry テーブルで 8 variant に縮約し ThemeRegistry の base slice に加算する
    /// theme 範囲外は Grass、 mask>=64 は slice 0 にフォールバック
    [[nodiscard]] std::uint16_t LookupTextureSlice(NS::Game::Theme::ThemeId theme, std::uint8_t neighborMask) noexcept;

} // namespace NS::Game::Blocks
