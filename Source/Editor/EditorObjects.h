#pragma once

// 配置物をエディタ側から扱う関数。出荷ビルドには載らない
// ゲームの判定の決まりを写す物だけは、判定の隣に置いて NS_SHIPPING で囲む

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Scene/SceneJson.h"

#include <string_view>

namespace NS::Obj
{
    class Actor;
    class ActorList;
} // namespace NS::Obj

namespace NS::Editor
{
    //! @brief cell ブラシが置換・削除・回転できる 1 m の升の配置物か
    //! @details 対象は組み込みの立方体と坂の地形の部品だけ。投げ込んだメッシュは大きさが升と限らない
    [[nodiscard]] bool IsCellBrushObject(const NS::Obj::Actor& object) noexcept;

    //! live 実体の cell 座標 = Root 位置を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const NS::Obj::Actor& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const NS::Obj::Actor& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const NS::Obj::Actor& object) noexcept;

    //! live の objects から cell 一致の最初の cell ブラシ配置物の永続 id。無ければ k_NoObjectId
    [[nodiscard]] std::uint32_t FindObjectIdAtCell(const NS::Obj::ActorList& objects,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    //! live の objects に、種類を問わず cell 一致の配置物が居る場合 true、それ以外の場合は false
    [[nodiscard]] bool HasPlacedObjectAtCell(const NS::Obj::ActorList& objects,
                                             std::int16_t x,
                                             std::int16_t y,
                                             std::int16_t z) noexcept;

    //! object の今の回転に、世界の Y 軸まわりの 90° を足す。傾きは残る
    void AddCellQuarterTurn(nlohmann::json& object) noexcept;

    //! object の回転を rotationStep に対応する Y 軸 yaw quaternion に設定する
    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept;

    //! @brief UI に出す名前。付けた名前があればそれ、無ければクラスの表示名
    //! @details 置けるクラスは登録の表示名、置けないクラスはクラス名、クラスの無い素の Actor は "Actor"
    [[nodiscard]] const char* ObjectDisplayName(const NS::Obj::Actor& object);

    //! @brief クリックで配置物を選ぶ時の判定箱を、Root のローカル空間で返す
    //! @details メッシュを描く配置物はメッシュの境界、描かない配置物 (カメラ・光・空の Actor) と
    //! メッシュが未解決の配置物は 1m 立方
    //! @param[in] object 判定箱を求める配置物
    //! @return Root のローカル空間の軸並行境界ボックス
    [[nodiscard]] NS::AABB PickLocalBounds(const NS::Obj::Actor& object) noexcept;

    //! シーンの JSON 文書からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
    //! 複数居ても先頭を正とする。2 体以上の警告は EnsurePlayerObject を通した時だけ出る
    [[nodiscard]] std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept;

    //! プレイヤーのひな形の JSON を作る。構成は Player の Init が決め、transform 以外の欄は書かない
    [[nodiscard]] nlohmann::json MakePlayerObject(const NS::Vector3& position, const NS::Quaternion& rotation);

    //! @brief プレイヤーが 1 体も居なければ既定構成で足し、永続 id まで振る。2 体以上なら警告して先頭を正とする
    //! @details 足す位置は DefaultSpawnPosition に、プレイヤーの種類の既定のカプセルを当てて求める
    //! @param[in,out] scene 補う先のシーンの JSON 文書
    //! @return 足した場合 true、それ以外の場合は false
    [[nodiscard]] bool EnsurePlayerObject(nlohmann::json& scene);

    //! 落下死の範囲が 1 つも無ければ既定の物を敷き、永続 id まで振る
    //! 無いレベルは奈落で死ねず落ち続けてしまうので、新しいレベルを作る時に通す
    [[nodiscard]] bool EnsureDeathZoneObject(nlohmann::json& scene);
} // namespace NS::Editor
