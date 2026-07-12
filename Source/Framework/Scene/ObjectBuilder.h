#pragma once

/// @file ObjectBuilder.h
/// @brief ObjectData 1 件を live GameObject へ写す汎用構築と、 live 構成からの疎な吸い出し
///
/// @details object.components を ComponentRegistry で生成し反射で値を入れる。 器に既に載る同型へは
/// 値だけを写して二重生成しない。 mesh / material 等の資産解決には関知せず、 組み上がった component
/// ごとに呼出側のコールバックへ委ねる。 逆方向は MakeObjectData が component 構成を型名だけの
/// データへ写し、 派生クラスの既定構成を手書きレシピへ並記させない
/// 依存: NS::Scene::GameObject / ObjectData, ComponentRegistry, ReflectionJson

#include "Framework/Scene/SceneData.h"

#include <functional>
#include <type_traits>

namespace NS::Scene
{
    class Component;
    class GameObject;

    /// component 1 個の生成と値適用が済むたびに呼ばれる。 資産解決など器へ載せた後の仕上げを呼出側が行う
    using ComponentBuiltFn = std::function<void(Component&, const ComponentData&)>;

    /// object.components を obj へ適用する。 器の既存同型には値だけを写し、 無い型は登録簿で生成する
    /// 同型を重ねたデータは上書きせず重ねた数だけ立て、 許可リスト外の型は読み飛ばす。 onBuilt は空でもよい
    void ApplyObjectComponents(GameObject& obj, const ObjectData& object, const ComponentBuiltFn& onBuilt);

    /// object の position / rotation / scale を obj の root transform へ写す
    void ApplyObjectTransform(GameObject& obj, const ObjectData& object) noexcept;

    /// obj の component 構成を型名だけの components に写した ObjectData を返す。 transform は既定のまま
    /// 値は写さずコード既定に任せる疎な写しで、 参照や配置などデータでしか決まらない値は呼出側が焼く
    /// 反射の無い component は型名を持てないため写らない
    [[nodiscard]] ObjectData MakeObjectData(const GameObject& obj);

    /// GameObject 派生 T の既定構成すなわちコンストラクタが積む component 構成から ObjectData を作る
    /// 派生クラスの構成を手書きレシピへ並記せず、 クラス自身を構成と既定値の唯一の出所にする
    template <class T> [[nodiscard]] ObjectData MakeObjectData()
    {
        static_assert(std::is_base_of_v<GameObject, T>, "T は GameObject 派生でなければならない");
        const T prototype{};
        return MakeObjectData(prototype);
    }

} // namespace NS::Scene
