#pragma once

/// @file LevelData.h
/// @brief LevelData — `.nslvl` に書く永続データ。 PlayMode は const 参照のみで受ける
///
/// @details Strict 分離: 永続フィールドはここに、 runtime mutable な playerPosition /
/// coinCount 等は `PlayState` に置く。 `PlayMode` 側で `const LevelData&` を要求する
/// ことで「PlayMode は LevelData を書き換えない」 を compile-time に保証する
/// `ComputeCrc32()` は field 単位の明示 update なので vector capacity 等の内部 padding
/// に依存せず、 同一データに対して常に同じ値を返す

#include <cstdint>
#include <vector>

namespace NS::Game::Level
{

    /// 1 block の永続表現 (10 byte、 natural alignment、 padding なし)
    /// rotation は 0/1/2/3 で 90° 刻みの Y 軸スナップ。 reserved は将来拡張枠で CRC32 hash 対象
    struct BlockEntry
    {
        std::int16_t x = 0;
        std::int16_t y = 0;
        std::int16_t z = 0;
        std::uint16_t blockId = 0;
        std::uint8_t rotation = 0;
        std::uint8_t reserved = 0;
    };
    static_assert(sizeof(BlockEntry) == 10, "BlockEntry must be 10 bytes (3×int16 + uint16 + 2×uint8)");

    /// `.nslvl` に書かれる永続データ。 PlayMode 中は const 参照でしか触らせない
    struct LevelData
    {
        std::vector<BlockEntry> blocks;

        std::int16_t spawnX = 0;
        std::int16_t spawnY = 0;
        std::int16_t spawnZ = 0;

        std::uint16_t themeId = 0;
        std::uint16_t bgmId = 0;
        std::uint16_t coinThreshold = 0;
        std::uint16_t timeLimitSeconds = 0;

        /// 全 field を明示的に CRC32 update して計算する
        /// vector の内部 padding (capacity と size の差) は対象外で、 `data()` から
        /// `size() * sizeof(BlockEntry)` byte だけ hash する
        [[nodiscard]] std::uint32_t ComputeCrc32() const noexcept;
    };

} // namespace NS::Game::Level
