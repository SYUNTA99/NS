#pragma once

// 配置物をエディタ側から扱うヘルパ
// cell ブラシの照合・90° 回転・Hierarchy の表示名・パレット雛形・クリックで選ぶ判定箱。出荷ビルドには載らない

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <string_view>

namespace NS::Obj
{
    class GameObject;
    class ObjectList;
} // namespace NS::Obj

namespace NS::Editor
{

    //! 配置物の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const nlohmann::json& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const nlohmann::json& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const nlohmann::json& object) noexcept;

    //! cell ブラシが置換 / 削除できる配置物か。見た目を持つ素の GameObject が対象
    [[nodiscard]] bool IsCellBrushObject(const nlohmann::json& object) noexcept;

    //! live 実体版。判定は JSON 版と同じ基準で、component の有無を実体から見る
    [[nodiscard]] bool IsCellBrushObject(NS::Obj::GameObject& object) noexcept;

    //! live 実体の cell 座標 = Root 位置を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const NS::Obj::GameObject& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const NS::Obj::GameObject& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const NS::Obj::GameObject& object) noexcept;

    //! cell の x, y, z に一致する最初の cell ブラシ配置物の添字。無ければ k_NoObjectIndex
    [[nodiscard]] std::size_t FindObjectAtCell(const nlohmann::json& scene,
                                               std::int16_t x,
                                               std::int16_t y,
                                               std::int16_t z) noexcept;

    //! live の objects から cell 一致の最初の cell ブラシ配置物の永続 id。無ければ k_NoObjectId
    [[nodiscard]] std::uint32_t FindObjectIdAtCell(const NS::Obj::ObjectList& objects,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    //! object の現在の 90° 回転 step を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t CellRotationStep(const nlohmann::json& object) noexcept;

    //! object の回転を rotationStep に対応する Y 軸 yaw quaternion に設定する
    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept;

    //! MeshRenderer の component entry を作る。Mesh / Material / Base Color を書き込む
    [[nodiscard]] nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                                       std::string_view materialName,
                                                       const NS::Core::Vector3& baseColor);

    //! 1m 立方の cube 描画と Box 当たりを積んだ、基本キューブの構成を作る
    [[nodiscard]] nlohmann::json MakeCellCubeComponents();

    //! cell の x, y, z に基本キューブのひな形の JSON を作る
    [[nodiscard]] nlohmann::json MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z);

    //! 指定角度のスロープの構成を作る
    [[nodiscard]] nlohmann::json MakeCellSlopeComponents(float angleDegrees);

    //! ヒエラルキーの右クリックから足せる基本形
    enum class PrimitiveKind
    {
        Empty,  //!< 何も持たない GameObject。子をぶら下げる支点や目印に使う
        Cube,   //!< 1m 立方の固形ブロック
        Sphere, //!< 半径 0.5 の球。当たりも球
        Slope,  //!< 45 度の坂
    };

    //! 基本形 1 個ぶんの component 構成を生成する。Empty は空配列
    [[nodiscard]] nlohmann::json MakePrimitiveComponents(PrimitiveKind kind);

    //! 接触判定と見た目を積んだゴールの構成を作る
    [[nodiscard]] nlohmann::json MakeGoalComponents();

    //! BoxCollider を持ち、ギミックでない固形ブロックか
    [[nodiscard]] bool IsSolidObject(const nlohmann::json& object);

    //! 90 度回転できるスロープか固形箱か
    [[nodiscard]] bool IsRotatableObject(const nlohmann::json& object);

    //! component 構成から UI 表示用の分類名を返す
    [[nodiscard]] const char* ObjectDisplayName(const nlohmann::json& object);

    //! live 実体版。判定は JSON 版と同じ基準で、component の有無と値を実体から見る
    [[nodiscard]] const char* ObjectDisplayName(NS::Obj::GameObject& object);

    //! @brief クリックで配置物を選ぶ時の判定箱を、Root のローカル空間で返す
    //! @details メッシュを描く配置物はメッシュの境界、描かない配置物 (カメラ・光・空の GameObject) と
    //! メッシュが未解決の配置物は 1m 立方
    //! @param[in] object 判定箱を求める配置物
    //! @return Root のローカル空間の軸並行境界ボックス
    [[nodiscard]] NS::Core::AABB PickLocalBounds(const NS::Obj::GameObject& object) noexcept;

} // namespace NS::Editor
