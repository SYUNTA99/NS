#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 値はコードの既定値 < 種類の既定値 < 個体の上書きの 3 段で重なる。種類の既定値は Archetype.h

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ObjectJson.h"

#include <memory>
#include <type_traits>

namespace NS::Obj
{
    class AssetManager;
    class SubObject;

    //! JSON の部品の件に、Actor がまだ持たない部品があった時にどうするか
    enum class SubObjCreation
    {
        Allow,
        Forbid,
    };

    //! 同じ部品名へ JSON の値を適用する。作れない部品は警告して読み飛ばす
    //! creation は不足する部品を追加するかを指定する
    void ApplyObjectSubObjs(Actor& obj, const nlohmann::json& object, SubObjCreation creation);

    //! 型登録から Actor を作り、種類の既定値と個体の値を適用する。id と名前は設定しない
    //! class がなければ nullptr。assets がなければ資産の解決を省く
    [[nodiscard]] std::unique_ptr<Actor> ObjectFromJson(const nlohmann::json& object, AssetManager* assets);

    //! @brief obj が自分を JSON へ書き出す。欄はクラスの種類の既定値と違う物 (個体の上書き) だけを書く
    [[nodiscard]] nlohmann::json ObjectToJson(const Actor& obj);

    //! @details transform 以外の値は写さずコード既定に任せる疎な写し。id は 0 で、置く時に振る
    [[nodiscard]] nlohmann::json MakePrototypeJson(const Actor& obj);

    //! @brief Actor 派生 T の OnInit が積む構成からひな形の JSON を作る
    //! @details 派生クラスの構成を手書きレシピへ並記せず、クラス自身を構成と既定値の唯一の出所にする
    template <class T> [[nodiscard]] nlohmann::json MakePrototypeJson()
    {
        static_assert(std::is_base_of_v<Actor, T>, "T は Actor 派生でなければならない");
        const std::unique_ptr<T> prototype = CreateActor<T>();
        return MakePrototypeJson(*prototype);
    }
} // namespace NS::Obj
