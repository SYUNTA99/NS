#pragma once

// 配置物をエディタ側から扱うヘルパ
// cell ブラシの照合・90° 回転・Hierarchy の表示名・クリックで選ぶ判定箱。出荷ビルドには載らない

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <string_view>

namespace NS::Obj
{
    class Actor;
    class ObjectList;
} // namespace NS::Obj

namespace NS::Editor
{
    //! 配置物の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const nlohmann::json& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const nlohmann::json& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const nlohmann::json& object) noexcept;

    //! cell ブラシが置換 / 削除できる配置物か。地形の部品 (MapParts) が対象
    [[nodiscard]] bool IsCellBrushObject(const nlohmann::json& object) noexcept;

    //! live 実体版。判定は JSON 版と同じ基準
    [[nodiscard]] bool IsCellBrushObject(const NS::Obj::Actor& object) noexcept;

    //! live 実体の cell 座標 = Root 位置を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const NS::Obj::Actor& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const NS::Obj::Actor& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const NS::Obj::Actor& object) noexcept;

    //! live の objects から cell 一致の最初の cell ブラシ配置物の永続 id。無ければ k_NoObjectId
    [[nodiscard]] std::uint32_t FindObjectIdAtCell(const NS::Obj::ObjectList& objects,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    //! object の現在の 90° 回転 step を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t CellRotationStep(const nlohmann::json& object) noexcept;

    //! object の回転を rotationStep に対応する Y 軸 yaw quaternion に設定する
    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept;

    //! 90 度回転できる配置物か。地形の部品
    [[nodiscard]] bool IsRotatableObject(const nlohmann::json& object);

    //! @brief UI に出す名前。付けた名前があればそれ、無ければクラスの表示名
    //! @details 置けるクラスは登録の表示名、置けないクラスはクラス名、クラスの無い素の Actor は "Actor"
    [[nodiscard]] const char* ObjectDisplayName(const nlohmann::json& object);

    //! live 実体版。判定は JSON 版と同じ基準
    [[nodiscard]] const char* ObjectDisplayName(const NS::Obj::Actor& object);

    //! @brief クリックで配置物を選ぶ時の判定箱を、Root のローカル空間で返す
    //! @details メッシュを描く配置物はメッシュの境界、描かない配置物 (カメラ・光・空の Actor) と
    //! メッシュが未解決の配置物は 1m 立方
    //! @param[in] object 判定箱を求める配置物
    //! @return Root のローカル空間の軸並行境界ボックス
    [[nodiscard]] NS::Core::AABB PickLocalBounds(const NS::Obj::Actor& object) noexcept;
} // namespace NS::Editor
