#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 資産解決には関知せず、組み上がった component ごとに呼出側のコールバックへ委ねる

#include "Runtime/Object/ObjectJson.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace NS::Obj
{
    class AssetManager;
    class Component;
    class GameObject;

    //! component 1 個の生成と値適用が済むたびに呼ばれる。第 2 引数はコンポーネント 1 件の JSON で、
    //! 資産解決など GameObject へ載せた後の仕上げは呼出側が担う
    using ComponentBuiltFn = std::function<void(Component&, const nlohmann::json&)>;

    //! @brief コンポーネント 1 件に対応する obj の既存 Component を返す。無ければ nullptr
    //! @details 名前を持つ件は同じ名前で同じ型の物を先に探す。名前の無い古いデータと、名前で見つからない件は、
    //! taken に無い同じ型の最初の 1 個を返す。組み立てと id の書き込みが同じ対応を引くための唯一の規則
    [[nodiscard]] Component* MatchComponentEntry(const GameObject& obj,
                                                 const nlohmann::json& entry,
                                                 const std::vector<Component*>& taken) noexcept;

    //! 配置物の JSON の components を obj へ適用する。GameObject の既存同型には値だけを写し、無い型は登録一覧から生成する
    //! 同型を重ねたデータは上書きせず重ねた数だけ立て、許可リスト外の型は読み飛ばす。onBuilt は空でもよい
    //! 名前を持つ件は Component の名前をそれに付け直す。id は書かない。書くのは配置物を積む ObjectList
    void ApplyObjectComponents(GameObject& obj, const nlohmann::json& object, const ComponentBuiltFn& onBuilt);

    //! @brief 配置物の JSON から配置物を組む唯一の汎用経路
    //! @details GameObject の型は TypeRegistry の class で選び、components を適用し、assets があれば各 component の
    //! ResolveAssets で参照を実体化する。id と名前は書かない。シーンへ積む ObjectList が書く
    //! components が空の JSON は配置物でないため nullptr。assets=nullptr (テスト等) は解決だけ跳ばす
    [[nodiscard]] std::unique_ptr<GameObject> ObjectFromJson(const nlohmann::json& object, AssetManager* assets);

    //! @brief obj が自分を JSON へ書き出す。全 component の全欄をリフレクションで写した忠実な写し
    //! @details 保存・undo の控え・プレイ開始時の凍結・複製が使う。リフレクションの無い component は型名を持てないため写らない
    [[nodiscard]] nlohmann::json ObjectToJson(const GameObject& obj);

    //! @brief obj の component 構成を型名と名前だけ写し、transform を書き込んだひな形の JSON を返す
    //! @details transform 以外の値は写さずコード既定に任せる疎な写し。id は 0 で、置く時に振る
    [[nodiscard]] nlohmann::json MakePrototypeJson(const GameObject& obj);

    //! @brief GameObject 派生 T のコンストラクタが積む構成からひな形の JSON を作る
    //! @details 派生クラスの構成を手書きレシピへ並記せず、クラス自身を構成と既定値の唯一の出所にする
    template <class T> [[nodiscard]] nlohmann::json MakePrototypeJson()
    {
        static_assert(std::is_base_of_v<GameObject, T>, "T は GameObject 派生でなければならない");
        const T prototype{};
        return MakePrototypeJson(prototype);
    }
} // namespace NS::Obj
