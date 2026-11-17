#pragma once

// アクターをエディタ側から扱う関数。出荷ビルドには載らない
// ゲームの判定の決まりを写す物だけは、判定の隣に置いて NS_SHIPPING で囲む

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS
{
    class ActorList;
} // namespace NS

namespace NS
{
    namespace Ed
    {
        //! アクターの JSON の姿勢を読み書きする。
        //! 実体は classObjs 内の姿勢の部品の欄。エディタと試しが Actor を作らずに置き場所を書くための関数
        //! Set は対象エントリが無ければ EnsureTransformClassObj で 1 つ作り、その項目だけを書く。書いた項目はシーンの上書きになる
        //! 読む側は、書いていない項目に Transform の初期値を返す
        [[nodiscard]] Vector3 ActorJsonPosition(const nlohmann::json& actorJson) noexcept;
        void SetActorJsonPosition(nlohmann::json& actorJson, const Vector3& position) noexcept;
        [[nodiscard]] Quaternion ActorJsonRotation(const nlohmann::json& actorJson) noexcept;
        void SetActorJsonRotation(nlohmann::json& actorJson, const Quaternion& rotation) noexcept;
        [[nodiscard]] Vector3 ActorJsonScale(const nlohmann::json& actorJson) noexcept;
        void SetActorJsonScale(nlohmann::json& actorJson, const Vector3& scale) noexcept;

        //! actorJson に Transform のエントリが無ければ、項目を持たない空のエントリを 1 つ足して返す
        //! 既にあればそれを返す。書かない項目は .actor の値になる
        nlohmann::json& EnsureTransformClassObj(nlohmann::json& actorJson);

        //! 位置のほかの値は写さず .actor の値に任せる疎な写し。id は 0 で、置く時に振る
        [[nodiscard]] nlohmann::json MakePrototypeJson(const SceneActor* obj);

        //! .actor の名前 actorFileName を指すひな形の JSON。アクターを作らずに組む
        //! 位置だけを Transform の初期値で書き、回転とスケールを含むほかの値は書かない。id は 0 で、置く時に振る
        [[nodiscard]] nlohmann::json MakePrototypeJson(std::string_view actorFileName);

        //! SceneActor 派生 T の OnInit が積む構成からひな形の JSON を作る
        //! 派生クラスの構成を手書きレシピへ並記せず、クラス自身を構成と既定値の唯一の出所にする
        template <class T> [[nodiscard]] nlohmann::json MakePrototypeJson()
        {
            static_assert(std::is_base_of_v<SceneActor, T>, "T は SceneActor 派生でなければならない");
            const std::unique_ptr<T> prototype = CreateActor<T>();
            return MakePrototypeJson(prototype.get());
        }

        //! エディタが並べて番号を振るアクター。objects の並びのまま
        //! アクター一覧・その数・参照先の候補はこの並びと番号で出す
        [[nodiscard]] std::vector<SceneActor*> PlacedActors(const ActorList& objects);

        //! id のアクターの今の姿を JSON へ書き出す。居なければ nullopt
        [[nodiscard]] std::optional<nlohmann::json> CaptureActor(ActorList& actors, uint32 id);

        //! actor の全部の部品の全部の欄の値。シーンの JSON と同じく classObjs の下に部品の鍵で並べる
        //! 編集の前に控え、編集の後に AddChangedSceneOverrides へ渡す
        [[nodiscard]] nlohmann::json AllFieldValues(const SceneActor* actor);

        //! before (AllFieldValues の形) から値が変わった、シーンに書く項目を actor の上書きの一覧に足す
        //! 参照の欄はいつも保存するので足さない。.actor へ書く項目は変わっていても触らない
        //! @return シーンに書く欄 (参照の欄を含む) が 1 つでも変わったか。.actor へ書く欄の変化は数えない
        [[nodiscard]] bool AddChangedSceneOverrides(SceneActor* actor, const nlohmann::json& before);

        //! roots と、その子孫の永続 id。roots の順の後に子孫を根に近い順で並べ、各 id を 1 回だけ入れる
        //! 親子が輪になったシーン (手で書いたファイル・インスペクタの参照の欄) でも 1 周で止まる
        [[nodiscard]] std::vector<uint32> CollectWithDescendants(const ActorList& actors,
                                                                 const std::vector<uint32>& roots);

        //! child の親を parent にしてよい場合 true。自分自身と自分の子孫は輪になるので false
        //! parent が k_NoActorId (親を外す) ならいつも true
        [[nodiscard]] bool CanReparent(const ActorList& actors, uint32 child, uint32 parent);

        //! 個体名を優先し、空なら登録の表示名かクラス名。未登録の型は SceneActor
        [[nodiscard]] const char* ActorDisplayName(const SceneActor* object);

        //! 選択判定に使う根の局所空間の AABB。メッシュ境界がなければ 1 m 立方
        [[nodiscard]] AABB PickLocalBounds(const SceneActor* object) noexcept;

        //! シーンの JSON 文書からプレイヤーを探す。最初の 1 件の添字、無ければ k_NoActorIndex
        //! 複数居ても先頭を正とする
        [[nodiscard]] std::size_t FindPlayerActorIndex(const nlohmann::json& scene) noexcept;
    } // namespace Ed
} // namespace NS
