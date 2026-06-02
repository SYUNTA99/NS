#pragma once

/// @file crc32.h
/// @brief CRC32 (polynomial 0xEDB88320) — 256-entry lookup table 実装
///
/// @details `.nslvl` footer の整合性検証専用、 LevelData の CRC32 と save format
/// で同じアルゴリズムを使う。 polynomial 選択は独自実装せず zlib 互換に固定
/// (deflate/PNG 等で実績のある polynomial を採用)

#include <cstddef>
#include <cstdint>
#include <span>

namespace NS::Game::Level::detail
{

    constexpr std::uint32_t kCrc32Init = 0xFFFFFFFFu;

    /// 1 ショット計算 (Init → Update → Finalize の糖衣)
    [[nodiscard]] std::uint32_t Crc32(std::span<const std::byte> bytes) noexcept;

    /// 継続計算用の Update。 初期値は `kCrc32Init`、 chunk 単位で複数回呼べる
    /// 返り値は次回の Update に渡す中間状態 (XOR 反転前)
    [[nodiscard]] std::uint32_t Crc32Update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept;

    /// Update 連結の最終値。 `crc ^ 0xFFFFFFFFu` を 1 回かけて返す
    [[nodiscard]] std::uint32_t Crc32Finalize(std::uint32_t crc) noexcept;

} // namespace NS::Game::Level::detail
