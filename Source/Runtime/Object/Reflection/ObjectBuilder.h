#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 値はコードの既定値 < 種類の既定値 < 個体の上書きの 3 段で重なる。種類の既定値は Archetype.h

#include "Runtime/Object/ObjectJson.h"

#include <memory>
#include <type_traits>
#include <vector>

namespace NS::Obj
{
    class AssetManager;
    class Component;
    class Actor;

    //! JSON の部品の件に、Actor がまだ持たない部品があった時にどうするか
    enum class PartCreation
    {
        Allow,  //!< 登録一覧から作って足す。種類の既定値が使う
        Forbid, //!< 足さずに読み飛ばす。個体のデータは値だけを上書きする
    };

    //! @brief コンポーネント 1 件に対応する obj の既存 Component を返す。無ければ nullptr
    //! @details 名前を持つ件は同じ名前で同じ型の物を先に探す。名前の無い古いデータと、名前で見つからない件は、
    //! taken に無い同じ型の最初の 1 個を返す。組み立てと id の書き込みが同じ対応を引くための唯一の規則
    [[nodiscard]] Component* MatchComponentEntry(const Actor& obj,
                                                 const nlohmann::json& entry,
                                                 const std::vector<Component*>& taken) noexcept;

    //! 配置物の JSON の components を obj へ適用する。Actor の既存部品には値だけを写す
    //! 無い部品は creation が Allow なら登録一覧から作り、Forbid なら読み飛ばす。許可リスト外の型も読み飛ばす
    //! 名前を持つ件は Component の名前をそれに付け直す。id は書かない。書くのは配置物を積む ObjectList
    void ApplyObjectComponents(Actor& obj, const nlohmann::json& object, PartCreation creation);

    //! @brief 配置物の JSON から配置物を組む唯一の汎用経路
    //! @details Actor の型は TypeRegistry の class で選ぶ。値はコードの既定値に種類の既定値 (ArchetypeLibrary) を重ね、
    //! その上に components の個体の上書きを重ねる。部品を足せるのは種類の既定値だけで、個体の件は値だけを写す
    //! assets があれば各 component の ResolveAssets で参照を実体化する。id と名前は書かない。シーンへ積む ObjectList が書く
    //! class の無い JSON は配置物でないため nullptr。assets=nullptr (テスト等) は解決だけ跳ばす
    [[nodiscard]] std::unique_ptr<Actor> ObjectFromJson(const nlohmann::json& object, AssetManager* assets);

    //! @brief obj が自分を JSON へ書き出す。欄はクラスの種類の既定値と違う物 (個体の上書き) だけを書く
    //! @details 保存・undo の控え・プレイ開始時の凍結・複製が使う。部品の件は id と名前を保つため全て書く
    //! 位置・回転・拡縮は常に書く。リフレクションの無い component は型名を持てないため写らない
    [[nodiscard]] nlohmann::json ObjectToJson(const Actor& obj);

    //! @brief obj の component 構成を型名と名前だけ写し、transform を書き込んだひな形の JSON を返す
    //! @details transform 以外の値は写さずコード既定に任せる疎な写し。id は 0 で、置く時に振る
    [[nodiscard]] nlohmann::json MakePrototypeJson(const Actor& obj);

    //! @brief Actor 派生 T のコンストラクタが積む構成からひな形の JSON を作る
    //! @details 派生クラスの構成を手書きレシピへ並記せず、クラス自身を構成と既定値の唯一の出所にする
    template <class T> [[nodiscard]] nlohmann::json MakePrototypeJson()
    {
        static_assert(std::is_base_of_v<Actor, T>, "T は Actor 派生でなければならない");
        const T prototype{};
        return MakePrototypeJson(prototype);
    }
} // namespace NS::Obj
