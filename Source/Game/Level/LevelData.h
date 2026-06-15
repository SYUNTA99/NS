#pragma once

/// @file LevelData.h
/// @brief LevelData — `.nslvl` に書く永続データ。 PlayMode は const 参照のみで受ける
///
/// @details Strict 分離: 永続フィールドはここに、 runtime mutable な playerPosition /
/// coinCount 等は `PlayState` に置く。 `PlayMode` 側で `const LevelData&` を要求する
/// ことで「PlayMode は LevelData を書き換えない」 を compile-time に保証する
/// `ComputeCrc32()` は field 単位の明示 update なので vector capacity 等の内部 padding
/// に依存せず、 同一データに対して常に同じ値を返す

#include <cstddef>
#include <cstdint>
#include <string>
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

    /// `ObjectInstance::flags` の bit。 グリッド配置物は bit0 を立て instancing / オートタイル対象にする
    inline constexpr std::uint8_t kObjectFlagGridAligned = 0x01;

    /// 配置物の永続表現 (48 byte、 natural alignment、 padding なし)
    /// grid block も自由配置物も同じ型で 1 リストに格納する。 grid かどうかは flags の bit0 で区別する
    /// position / rotation(quaternion) / scale をフル保持し、 kind は BlockRegistry の blockId を流用する
    /// materialIndex は `LevelData::materialPaths` への添字、 -1 は kind 既定マテリアルを表す
    struct ObjectInstance
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
        float rotationW = 1.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
        std::uint16_t kind = 0;
        std::int16_t materialIndex = -1;
        std::uint8_t flags = 0;
        std::uint8_t reserved0 = 0;
        std::uint16_t reserved1 = 0;
    };
    static_assert(sizeof(ObjectInstance) == 48,
                  "ObjectInstance must be 48 bytes (10×float + uint16 + int16 + 2×uint8 + uint16)");
    static_assert(std::is_trivially_copyable_v<ObjectInstance>,
                  "ObjectInstance must be trivially copyable for memcpy I/O");

    /// `.nslvl` に書かれる永続データ。 PlayMode 中は const 参照でしか触らせない
    struct LevelData
    {
        /// 唯一の配置物リスト (grid block も自由配置物も含む)。 grid かどうかは各要素の flags で判別する
        std::vector<ObjectInstance> objects;

        /// objects の materialIndex が参照する .mat 相対パス表
        std::vector<std::string> materialPaths;

        std::int16_t spawnX = 0;
        std::int16_t spawnY = 0;
        std::int16_t spawnZ = 0;

        std::uint16_t themeId = 0;
        std::uint16_t bgmId = 0;
        std::uint16_t coinThreshold = 0;
        std::uint16_t timeLimitSeconds = 0;

        /// field 単位の明示 update で計算。 vector は `data()+size()*sizeof(element)` のみ対象 (capacity 除外)
        [[nodiscard]] std::uint32_t ComputeCrc32() const noexcept;
    };

    /// objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t kNoObjectIndex = static_cast<std::size_t>(-1);

    /// gridAligned object の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const ObjectInstance& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const ObjectInstance& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const ObjectInstance& object) noexcept;

    /// gridAligned かつ cell (x, y, z) に一致する最初の object の添字。 無ければ kNoObjectIndex
    [[nodiscard]] std::size_t FindGridObjectAtCell(const LevelData& level,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    /// cell (x, y, z) + kind + rotationStep(0..3) から gridAligned な ObjectInstance を作る
    [[nodiscard]] ObjectInstance MakeGridObject(
        std::int16_t x, std::int16_t y, std::int16_t z, std::uint16_t kind, std::uint8_t rotationStep) noexcept;

    /// gridAligned object の現在の 90° 回転 step(0..3) を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t GridRotationStep(const ObjectInstance& object) noexcept;

    /// gridAligned object の回転を step(0..3) の Y 軸 yaw quaternion に設定する
    void SetGridRotationStep(ObjectInstance& object, std::uint8_t rotationStep) noexcept;

    /// 旧 BLKS の BlockEntry 群を ObjectInstance に変換して `level.objects` 末尾へ追加する (gridAligned を立てる)
    /// rotation(0..3) は Y 軸 yaw quaternion に、 セル整数座標は world 座標 float に写す
    void MigrateBlocksToObjects(LevelData& level, const std::vector<BlockEntry>& blocks) noexcept;

} // namespace NS::Game::Level
