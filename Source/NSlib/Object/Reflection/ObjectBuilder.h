#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 値はコードの既定値 < 種類の既定値 < 個体の上書きの 3 段で重なる。種類の既定値は Archetype.h

#include "NSlib/Object/ObjectJson.h"

#include <memory>
#include <type_traits>

namespace NS::Obj
{
    class AssetManager;
    class SubObject;
    class Actor;

    //! JSON の部品の件に、Actor がまだ持たない部品があった時にどうするか
    enum class SubObjCreation
    {
        Allow,
        Forbid,
    };

    //! @brief 配置物の JSON の "subObjects" の欄を、同じ部品名の obj の部品へ書く
    //! @details 部品を作れないか作らない時は警告を出してその部品を読み飛ばす
    //! @param[in,out] obj 書く先の配置物
    //! @param[in] object 配置物の JSON
    //! @param[in] creation obj がまだ持たない部品を CreateSubObj で作るか
    void ApplyObjectSubObjs(Actor& obj, const nlohmann::json& object, SubObjCreation creation);

    //! @brief 配置物の JSON から配置物を組む唯一の汎用経路
    //! @details Actor の型は TypeRegistry の class で選ぶ。値はコードの既定値に種類の既定値 (ArchetypeLibrary) を重ね、
    //! assets があれば各 SubObject の ResolveAssets で参照を実体化する。id と名前は書かない。シーンへ積む ActorList
    //! が書く class の無い JSON は配置物でないため nullptr。assets=nullptr (テスト等) は解決だけ跳ばす
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
        T prototype{};
        prototype.Init();
        return MakePrototypeJson(prototype);
    }
} // namespace NS::Obj
