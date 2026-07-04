#pragma once

/// @file crc32.h
/// @brief CRC32 polynomial 0xEDB88320 の 256-entry lookup table 実装
///
/// @details `.scene` footer の整合性検証専用。 LevelData の CRC32 と save format で同じアルゴリズムを使う
/// polynomial は 0xEDB88320 固定で IEEE 802.3 / zlib 互換

#include <cstddef>
#include <cstdint>
#include <span>

namespace NS::Game::Level::detail
{

    constexpr std::uint32_t kCrc32Init = 0xFFFFFFFFu;

    /// 継続計算用の Update。 初期値は `kCrc32Init`、 chunk 単位で複数回呼べる
    /// 返り値は次回の Update に渡す XOR 反転前の中間状態
    [[nodiscard]] std::uint32_t Crc32Update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept;

    /// Update 連結の最終値。 `crc ^ 0xFFFFFFFFu` を 1 回かけて返す
    [[nodiscard]] std::uint32_t Crc32Finalize(std::uint32_t crc) noexcept;

} // namespace NS::Game::Level::detail
