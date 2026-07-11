#pragma once

/// @file ObjectBuilder.h
/// @brief ObjectData 1 件を live GameObject へ写す汎用構築
///
/// @details object.components を ComponentRegistry で生成し反射で値を入れる。 器に既に載る同型へは
/// 値だけを写して二重生成しない。 mesh / material 等の資産解決には関知せず、 組み上がった component
/// ごとに呼出側のコールバックへ委ねる
/// 依存: NS::Scene::GameObject / ObjectData, ComponentRegistry, ReflectionJson

#include <functional>

namespace NS::Scene
{
    class Component;
    class GameObject;
    struct ComponentData;
    struct ObjectData;

    /// component 1 個の生成と値適用が済むたびに呼ばれる。 資産解決など器へ載せた後の仕上げを呼出側が行う
    using ComponentBuiltFn = std::function<void(Component&, const ComponentData&)>;

    /// object.components を obj へ適用する。 器の既存同型には値だけを写し、 無い型は登録簿で生成する
    /// 同型を重ねたデータは上書きせず重ねた数だけ立て、 許可リスト外の型は読み飛ばす。 onBuilt は空でもよい
    void ApplyObjectComponents(GameObject& obj, const ObjectData& object, const ComponentBuiltFn& onBuilt);

    /// object の position / rotation / scale を obj の root transform へ写す
    void ApplyObjectTransform(GameObject& obj, const ObjectData& object) noexcept;

} // namespace NS::Scene
