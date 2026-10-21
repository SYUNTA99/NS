#pragma once

// 配置物をエディタ側から扱う関数。出荷ビルドには載らない
// ゲームの判定の決まりを写す物だけは、判定の隣に置いて NS_SHIPPING で囲む

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS
{
    class ActorList;
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! 配置物の JSON の姿勢を読み書きする。
        //! 実体は classObjs 内の姿勢の部品の欄。エディタと試しが Actor を作らずに置き場所を書くための関数
        //! Set は対象エントリが無ければ EnsureTransformClassObj で 1 つ作る
        [[nodiscard]] Vector3 ObjectPosition(const nlohmann::json& object) noexcept;
        void SetObjectPosition(nlohmann::json& object, const Vector3& position) noexcept;
        [[nodiscard]] Quaternion ObjectRotation(const nlohmann::json& object) noexcept;
        void SetObjectRotation(nlohmann::json& object, const Quaternion& rotation) noexcept;
        [[nodiscard]] Vector3 ObjectScale(const nlohmann::json& object) noexcept;
        void SetObjectScale(nlohmann::json& object, const Vector3& scale) noexcept;

        //! object に Transform のエントリが無ければ既定値(位置0 / 回転なし / スケール1)で 1 つ足して返す
        //! 既にあればそれを返す
        nlohmann::json& EnsureTransformClassObj(nlohmann::json& object);

        //! @details transform 以外の値は写さずコード既定に任せる疎な写し。id は 0 で、置く時に振る
        [[nodiscard]] nlohmann::json MakePrototypeJson(const SceneActor& obj);

        //! @brief SceneActor 派生 T の OnInit が積む構成からひな形の JSON を作る
        //! @details 派生クラスの構成を手書きレシピへ並記せず、クラス自身を構成と既定値の唯一の出所にする
        template <class T> [[nodiscard]] nlohmann::json MakePrototypeJson()
        {
            static_assert(std::is_base_of_v<SceneActor, T>, "T は SceneActor 派生でなければならない");
            const std::unique_ptr<T> prototype = CreateActor<T>();
            return MakePrototypeJson(*prototype);
        }

        //! @brief エディタが並べて番号を振る配置物。objects の並びのまま
        //! @details アクター一覧・その数・参照先の候補はこの並びと番号で出す
        [[nodiscard]] std::vector<SceneActor*> PlacedActors(const ActorList& objects);

        //! @brief cell ブラシが置換・削除・回転できる 1 m の升の配置物か
        //! @details 対象は組み込みの立方体と坂の地形の部品だけ。投げ込んだメッシュは大きさが升と限らない。
        [[nodiscard]] bool IsCellBrushObject(const SceneActor& object) noexcept;

        //! 実体のセル座標。Root の位置を最も近い整数へ丸めた値
        [[nodiscard]] int16 ObjectCellX(const SceneActor& object) noexcept;
        [[nodiscard]] int16 ObjectCellY(const SceneActor& object) noexcept;
        [[nodiscard]] int16 ObjectCellZ(const SceneActor& object) noexcept;

        //! objects からセルが一致する最初のセルブラシ配置物の永続識別番号を返す。不在は k_NoObjectId
        [[nodiscard]] uint32 FindObjectIdAtCell(const ActorList& objects,
                                                    int16 x,
                                                    int16 y,
                                                    int16 z) noexcept;

        //! objects に種類を問わずセルが一致する配置物がある場合 true、それ以外の場合は false
        [[nodiscard]] bool HasPlacedObjectAtCell(const ActorList& objects,
                                                 int16 x,
                                                 int16 y,
                                                 int16 z) noexcept;

        //! object の今の回転に、世界の Y 軸まわりの 90° を足す。傾きは残る
        void AddCellQuarterTurn(nlohmann::json& object) noexcept;

        //! object の回転を rotationStep に対応する Y 軸回転のクォータニオンに設定する
        void SetCellRotationStep(nlohmann::json& object, uint8 rotationStep) noexcept;

        //! 個体名を優先し、空なら登録の表示名かクラス名。未登録の型は SceneActor
        [[nodiscard]] const char* ObjectDisplayName(const SceneActor& object);

        //! 選択判定に使う根の局所空間の AABB。メッシュ境界がなければ 1 m 立方
        [[nodiscard]] AABB PickLocalBounds(const SceneActor& object) noexcept;

        //! シーンの JSON 文書からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
        //! 複数居ても先頭を正とする。2 体以上の警告は EnsurePlayerObject を通した時だけ出る
        [[nodiscard]] std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept;

        //! プレイヤーのひな形の JSON を作る。構成は PlayerActor の OnInit が決め、transform 以外の欄は書かない
        [[nodiscard]] nlohmann::json MakePlayerObject(const Vector3& position, const Quaternion& rotation);

        //! 不在なら PlayerActor.actor の構成でプレイヤーを追加し、採番する。追加時だけ true
        //! 足元は DefaultSpawnPosition とカプセル寸法から決める。複数いれば警告し、先頭を使う
        [[nodiscard]] bool EnsurePlayerObject(nlohmann::json& scene);

        //! 落下死の範囲が 1 つも無ければ既定の物を敷き、永続 id まで振る
        //! 無いレベルは奈落で死ねず落ち続けてしまうので、新しいレベルを作る時に通す
        [[nodiscard]] bool EnsureDeathZoneObject(nlohmann::json& scene);
    } // namespace Editor
} // namespace NS
