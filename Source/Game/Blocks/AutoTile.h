#pragma once

/// @file AutoTile.h
/// @brief 地形オートタイリング — 6 方向 neighbor の bitmask 計算 + slice 帯からの slice 引き
///
/// @details `ComputeNeighborMask` で 6 面 bitmask を算出し、 `LookupTextureSlice` で
/// `(baseSlice, mask)` を `TextureArray` の slice index に変換する。 slice 引きは
/// 64 entry の縮約テーブルで mask を 8 variant に丸め、 シーンの block slice 帯の先頭に加算する
/// 連結判定はメッシュ参照とマテリアルの同一性で行う

#include <cstdint>

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

    /// 6-neighbor mask を 64 entry テーブルで 8 variant に縮約し slice 帯の先頭 baseSlice に加算する
    /// mask>=64 は baseSlice をそのまま、 加算結果が総 slice 数以上なら 0 にフォールバックする
    [[nodiscard]] std::uint16_t LookupTextureSlice(std::uint16_t baseSlice, std::uint8_t neighborMask) noexcept;

} // namespace NS::Game::Blocks
