#pragma once

// 配置物をエディタの操作語彙で扱うヘルパ
// cell ブラシの照合・90° 回転・Hierarchy の表示名・パレット雛形。出荷ビルドには載らない

#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Object
{
    class GameObject;
    class World;
} // namespace NS::Object

namespace NS::Editor
{

    /// 配置物の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const NS::Object::ObjectData& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const NS::Object::ObjectData& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const NS::Object::ObjectData& object) noexcept;

    /// cell ブラシが置換 / 削除できる配置物か。プレイヤーとカメラは別経路で扱うため除く
    [[nodiscard]] bool IsCellBrushObject(const NS::Object::ObjectData& object) noexcept;

    /// live 実体版。判定はデータ版と同じ基準で、component の有無を実体から見る
    [[nodiscard]] bool IsCellBrushObject(NS::Object::GameObject& object) noexcept;

    /// live 実体の cell 座標 = Root 位置を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const NS::Object::GameObject& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const NS::Object::GameObject& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const NS::Object::GameObject& object) noexcept;

    /// cell の x, y, z に一致する最初の cell ブラシ配置物の添字。無ければ k_NoObjectIndex
    [[nodiscard]] std::size_t FindObjectAtCell(const NS::Object::SceneData& level,
                                               std::int16_t x,
                                               std::int16_t y,
                                               std::int16_t z) noexcept;

    /// live の world から cell 一致の最初の cell ブラシ配置物の永続 id。無ければ k_NoObjectId
    [[nodiscard]] std::uint32_t FindObjectIdAtCell(const NS::Object::World& world,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    /// object の現在の 90° 回転 step を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t CellRotationStep(const NS::Object::ObjectData& object) noexcept;

    /// object の回転を rotationStep に対応する Y 軸 yaw quaternion に設定する
    void SetCellRotationStep(NS::Object::ObjectData& object, std::uint8_t rotationStep) noexcept;

    /// 指定角度のスロープ（楔）構成を生成する
    [[nodiscard]] nlohmann::json MakeCellSlopeComponents(float angleDegrees);

    /// ヒエラルキーの右クリックから足せる基本形
    enum class PrimitiveKind
    {
        Empty,  ///< 何も持たない GameObject。 子をぶら下げる支点や目印に使う
        Cube,   ///< 1m 立方の固形ブロック
        Sphere, ///< 半径 0.5 の球。 当たりも球
        Slope,  ///< 45 度の坂
    };

    /// 基本形 1 個ぶんの component 構成を生成する。 Empty は空配列
    [[nodiscard]] nlohmann::json MakePrimitiveComponents(PrimitiveKind kind);

    /// ゴール（接触判定＋視覚モデル）の構成を生成する
    [[nodiscard]] nlohmann::json MakeGoalComponents();

    /// 固形ブロック（BoxCollider を持ち、ギミック用途でない）か判定する
    [[nodiscard]] bool IsSolidObject(const NS::Object::ObjectData& object);

    /// 90 度回転操作の対象（スロープまたは固形箱）か判定する
    [[nodiscard]] bool IsRotatableObject(const NS::Object::ObjectData& object);

    /// コンポーネント構成から UI 表示用の分類名（ASCII 固定文字列）を判定して返す
    [[nodiscard]] const char* ObjectDisplayName(const NS::Object::ObjectData& object);

    /// live 実体版。判定はデータ版と同じ基準で、component の有無と値を実体から見る
    [[nodiscard]] const char* ObjectDisplayName(NS::Object::GameObject& object);

} // namespace NS::Editor
