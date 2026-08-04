#pragma once

// ObjectData 1 件を live GameObject へ写す汎用構築と、 live 構成からの型名だけの吸い出し
// 資産解決には関知せず、 組み上がった component ごとに呼出側のコールバックへ委ねる

#include "Runtime/Object/Scene/SceneData.h"

#include <functional>
#include <memory>
#include <type_traits>

namespace NS::Object
{
    class AssetManager;
    class Component;
    class GameObject;

    /// component 1 個の生成と値適用が済むたびに呼ばれる。 第 2 引数は {"type", "fields"} のコンポーネント 1 件で、
    /// 資産解決など GameObject へ載せた後の仕上げは呼出側が担う
    using ComponentBuiltFn = std::function<void(Component&, const nlohmann::json&)>;

    /// object.components を obj へ適用する。 GameObject の既存同型には値だけを写し、 無い型は登録一覧から生成する
    /// 同型を重ねたデータは上書きせず重ねた数だけ立て、 許可リスト外の型は読み飛ばす。 onBuilt は空でもよい
    void ApplyObjectComponents(GameObject& obj, const ObjectData& object, const ComponentBuiltFn& onBuilt);

    /// object 1 件から配置物を組む唯一の汎用経路。 GameObject の型は TypeRegistry の className で選び、
    /// components を適用し、 assets があれば各 component の ResolveAssets で参照を実体化する
    /// components が空の object は配置物でないため nullptr。 assets=nullptr (テスト等) は解決だけ跳ばす
    [[nodiscard]] std::unique_ptr<GameObject> BuildSceneObject(const ObjectData& object, AssetManager* assets);

    /// obj の component 構成を型名だけの components に写し、 transform を TransformComponent エントリへ焼いた
    /// ObjectData を返す。 transform 以外の値は写さずコード既定に任せる疎な写しで、
    /// 参照や配置などデータでしか決まらない値は呼出側が焼く。 反射の無い component は型名を持てないため写らない
    [[nodiscard]] ObjectData MakeObjectData(const GameObject& obj);

    /// obj の全 component を反射で読み、 型名と全フィールド値を写した忠実な ObjectData を返す
    /// 疎な MakeObjectData と違い既定と同値の欄も含めて丸ごと写す、 undo とプレイ中の変化を編集へ持ち込まないための値退避に使う
    /// 反射の無い component は型名を持てないため写らない。 復元は ApplyObjectComponents が担う
    [[nodiscard]] ObjectData CaptureObjectData(const GameObject& obj);

    /// GameObject 派生 T の既定構成すなわちコンストラクタが積む component 構成から ObjectData を作る
    /// 派生クラスの構成を手書きレシピへ並記せず、 クラス自身を構成と既定値の唯一の出所にする
    template <class T> [[nodiscard]] ObjectData MakeObjectData()
    {
        static_assert(std::is_base_of_v<GameObject, T>, "T は GameObject 派生でなければならない");
        const T prototype{};
        return MakeObjectData(prototype);
    }

} // namespace NS::Object
