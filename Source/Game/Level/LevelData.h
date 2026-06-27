#pragma once

/// @file LevelData.h
/// @brief LevelData — `.nslvl` に書く永続データ。 PlayMode は const 参照のみで受ける
///
/// @details Strict 分離: 永続フィールドはここに、 runtime mutable な playerPosition /
/// coinCount 等は `PlayState` に置く。 `PlayMode` 側で `const LevelData&` を要求する
/// ことで「PlayMode は LevelData を書き換えない」 を compile-time に保証する
/// `ComputeCrc32()` は field 単位の明示 update なので vector capacity 等の内部 padding
/// に依存せず、 同一データに対して常に同じ値を返す

#include "Framework/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
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

    /// 当たり判定の形状種別。 `ObjectInstance::shapeCollider` に格納する (既定 / 旧データは Box)
    enum class ShapeCollider : std::uint8_t
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        Mesh = 3,
    };

    /// 反射 1 フィールドの永続値。 name は反射フィールド名、 value の代替型は FieldType と 1:1
    struct FieldValue
    {
        std::string name;
        /// 変種の宣言順に ComputeCrc32 と operator== の switch が依存する。 増減・並べ替え時は両方を直す
        std::variant<float, int, bool, NS::Math::Vector3, std::string> value;

        /// variant の Vector3 代替が operator== を持たないため代替ごとに明示比較する
        [[nodiscard]] bool operator==(const FieldValue& other) const noexcept;
    };

    /// 1 コンポーネントの永続表現。 型名 + 反射フィールド値一覧
    struct ComponentData
    {
        std::string typeName;
        std::vector<FieldValue> fields;

        /// fields 比較は FieldValue::operator== に委譲される
        [[nodiscard]] bool operator==(const ComponentData& other) const = default;
    };

    /// 配置物の永続表現。 コンポーネント一覧を内包する full SSOT 表現
    /// grid block も自由配置物も同じ型で 1 リストに格納する。 grid かどうかは flags の bit0 で区別する
    /// position / rotation(quaternion) / scale をフル保持し、 種別は components が表す
    /// materialIndex は `LevelData::materialPaths` への添字、 -1 は既定マテリアルを表す
    /// colliderHalfExtents は Transform と独立した当たり箱の local 半径 (既定 0.5)。 world では Transform.scale が乗る
    /// colliderOffset / colliderRotation は当たり箱を視覚と独立に owner local 空間でずらす / 回す (既定 0 / 単位)
    /// shapeCollider は当たり判定形状 (Box/Sphere/Capsule/Mesh、 既定 / 旧データは Box)
    /// colliderHalfExtents は形状で解釈が変わる: Box=各半径 / Sphere=x が半径 / Capsule=x 半径・y 半高 / Mesh=未使用
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
        std::int16_t materialIndex = -1;
        std::uint8_t flags = 0;
        std::uint8_t shapeCollider = 0;
        std::uint16_t reserved1 = 0;
        float colliderHalfExtentsX = 0.5f;
        float colliderHalfExtentsY = 0.5f;
        float colliderHalfExtentsZ = 0.5f;
        float colliderOffsetX = 0.0f;
        float colliderOffsetY = 0.0f;
        float colliderOffsetZ = 0.0f;
        float colliderRotationX = 0.0f;
        float colliderRotationY = 0.0f;
        float colliderRotationZ = 0.0f;
        float colliderRotationW = 1.0f;

        /// このオブジェクトが持つコンポーネント一覧。 full SSOT のコンポ構成
        std::vector<ComponentData> components;

        [[nodiscard]] bool operator==(const ObjectInstance& other) const = default;
    };

    /// エリア進入で切り替わる据え置きカメラ 1 件の永続表現 (56 byte、 natural alignment、 padding なし)
    /// camera* がカメラ視点、 trigger* がプレイヤー進入を判定する AABB (中心 + 半径)
    /// priority が高いほど他 vcam を上回って選ばれる。 lookAtPlayer が 0 以外なら
    /// lookTarget を無視してプレイヤーを追視する (位置固定で被写体を追う Mario 系の挙動)
    struct CameraVolume
    {
        float cameraPositionX = 0.0f;
        float cameraPositionY = 0.0f;
        float cameraPositionZ = 0.0f;
        float lookTargetX = 0.0f;
        float lookTargetY = 0.0f;
        float lookTargetZ = 0.0f;
        float triggerCenterX = 0.0f;
        float triggerCenterY = 0.0f;
        float triggerCenterZ = 0.0f;
        float triggerExtentX = 1.0f;
        float triggerExtentY = 1.0f;
        float triggerExtentZ = 1.0f;
        std::int32_t priority = 10;
        std::uint8_t lookAtPlayer = 0;
        std::uint8_t reserved0 = 0;
        std::uint16_t reserved1 = 0;
    };
    static_assert(sizeof(CameraVolume) == 56, "CameraVolume must be 56 bytes (12×float + int32 + 2×uint8 + uint16)");
    static_assert(std::is_trivially_copyable_v<CameraVolume>, "CameraVolume must be trivially copyable for memcpy I/O");

    /// `.nslvl` に書かれる永続データ。 PlayMode 中は const 参照でしか触らせない
    struct LevelData
    {
        /// 唯一の配置物リスト (grid block も自由配置物も含む)。 grid かどうかは各要素の flags で判別する
        std::vector<ObjectInstance> objects;

        /// objects の materialIndex が参照する .mat 相対パス表
        std::vector<std::string> materialPaths;

        /// エリアカメラの永続リスト。 objects とは別管理 (視点 + トリガ範囲を 1 件で持つため箱型に入らない)
        std::vector<CameraVolume> cameraVolumes;

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

    /// object の collider 形状を返す。 未知値は安全側で Box に倒す
    [[nodiscard]] ShapeCollider ObjectShapeCollider(const ObjectInstance& object) noexcept;
    /// object の collider 形状を設定する
    void SetObjectShapeCollider(ObjectInstance& object, ShapeCollider shape) noexcept;

    /// gridAligned object の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const ObjectInstance& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const ObjectInstance& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const ObjectInstance& object) noexcept;

    /// gridAligned かつ cell (x, y, z) に一致する最初の object の添字。 無ければ kNoObjectIndex
    [[nodiscard]] std::size_t FindGridObjectAtCell(const LevelData& level,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    /// cell (x, y, z) + rotationStep(0..3) から gridAligned な既定 solid の ObjectInstance を作る
    /// 既定 solid 一式 (cube 描画 + Box 当たり) を component として積む
    [[nodiscard]] ObjectInstance MakeGridObject(std::int16_t x,
                                                std::int16_t y,
                                                std::int16_t z,
                                                std::uint8_t rotationStep);

    /// gridAligned object の現在の 90° 回転 step(0..3) を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t GridRotationStep(const ObjectInstance& object) noexcept;

    /// gridAligned object の回転を step(0..3) の Y 軸 yaw quaternion に設定する
    void SetGridRotationStep(ObjectInstance& object, std::uint8_t rotationStep) noexcept;

    /// 旧 BLKS の BlockEntry 群を ObjectInstance に変換して `level.objects` 末尾へ追加する (gridAligned を立てる)
    /// rotation(0..3) は Y 軸 yaw quaternion に、 セル整数座標は world 座標 float に写す
    void MigrateBlocksToObjects(LevelData& level, const std::vector<BlockEntry>& blocks);

} // namespace NS::Game::Level
