#pragma once

// 配置物をエディタ側から扱う関数。出荷ビルドには載らない
// ゲームの判定の決まりを写す物だけは、判定の隣に置いて NS_SHIPPING で囲む

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <string_view>

namespace NS
{
    class SceneActor;
    class ActorList;
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! @brief cell ブラシが置換・削除・回転できる 1 m の升の配置物か
        //! @details 対象は組み込みの立方体と坂の地形の部品だけ。投げ込んだメッシュは大きさが升と限らない
        [[nodiscard]] bool IsCellBrushObject(const NS::SceneActor& object) noexcept;

        //! 実体のセル座標。Root の位置を最も近い整数へ丸めた値
        [[nodiscard]] NS::int16 ObjectCellX(const NS::SceneActor& object) noexcept;
        [[nodiscard]] NS::int16 ObjectCellY(const NS::SceneActor& object) noexcept;
        [[nodiscard]] NS::int16 ObjectCellZ(const NS::SceneActor& object) noexcept;

        //! objects からセルが一致する最初のセルブラシ配置物の永続識別番号を返す。不在は k_NoObjectId
        [[nodiscard]] NS::uint32 FindObjectIdAtCell(const NS::ActorList& objects,
                                                    NS::int16 x,
                                                    NS::int16 y,
                                                    NS::int16 z) noexcept;

        //! objects に種類を問わずセルが一致する配置物がある場合 true、それ以外の場合は false
        [[nodiscard]] bool HasPlacedObjectAtCell(const NS::ActorList& objects,
                                                 NS::int16 x,
                                                 NS::int16 y,
                                                 NS::int16 z) noexcept;

        //! object の今の回転に、世界の Y 軸まわりの 90° を足す。傾きは残る
        void AddCellQuarterTurn(nlohmann::json& object) noexcept;

        //! object の回転を rotationStep に対応する Y 軸回転のクォータニオンに設定する
        void SetCellRotationStep(nlohmann::json& object, NS::uint8 rotationStep) noexcept;

        //! 個体名を優先し、空なら登録の表示名かクラス名。未登録の型は SceneActor
        [[nodiscard]] const char* ObjectDisplayName(const NS::SceneActor& object);

        //! 選択判定に使う根の局所空間の AABB。メッシュ境界がなければ 1 m 立方
        [[nodiscard]] NS::AABB PickLocalBounds(const NS::SceneActor& object) noexcept;

        //! シーンの JSON 文書からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
        //! 複数居ても先頭を正とする。2 体以上の警告は EnsurePlayerObject を通した時だけ出る
        [[nodiscard]] std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept;

        //! プレイヤーのひな形の JSON を作る。構成は PlayerActor の OnInit が決め、transform 以外の欄は書かない
        [[nodiscard]] nlohmann::json MakePlayerObject(const NS::Vector3& position, const NS::Quaternion& rotation);

        //! 不在なら種類の既定構成でプレイヤーを追加し、採番する。追加時だけ true
        //! 足元は DefaultSpawnPosition とカプセル寸法から決める。複数いれば警告し、先頭を使う
        [[nodiscard]] bool EnsurePlayerObject(nlohmann::json& scene);

        //! 落下死の範囲が 1 つも無ければ既定の物を敷き、永続 id まで振る
        //! 無いレベルは奈落で死ねず落ち続けてしまうので、新しいレベルを作る時に通す
        [[nodiscard]] bool EnsureDeathZoneObject(nlohmann::json& scene);
    } // namespace Editor
} // namespace NS
