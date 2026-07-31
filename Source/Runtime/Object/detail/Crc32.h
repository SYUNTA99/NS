#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace NS::Object::detail
{

    /// @brief SceneData の整合性検証用 CRC32。256-entry lookup table 実装
    /// @details polynomial は 0xEDB88320 固定で IEEE 802.3 / zlib 互換。保存形式と同じアルゴリズムを使う
    inline constexpr std::uint32_t k_Crc32Init = 0xFFFFFFFFu;

    /// 継続計算用の Update。 初期値は `k_Crc32Init`、 chunk 単位で複数回呼べる
    /// 返り値は次回の Update に渡す XOR 反転前の中間状態
    [[nodiscard]] std::uint32_t Crc32Update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept;

    /// Update 連結の最終値。 `crc ^ 0xFFFFFFFFu` を 1 回かけて返す
    [[nodiscard]] std::uint32_t Crc32Finalize(std::uint32_t crc) noexcept;

} // namespace NS::Object::detail
