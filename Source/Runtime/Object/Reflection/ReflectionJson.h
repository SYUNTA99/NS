#pragma once

#include "Runtime/Object/Component.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

// リフレクションを辿って Component を {type, fields} JSON へ相互変換する
// 値は nlohmann::json を直接受け渡し、呼出側は object 配列へそのまま積める

namespace NS::Object
{
    //! comp を {"type": リフレクション typeName, "fields": {名前: 値}} の JSON object へ書き出す
    //! GetReflection() が nullptr のリフレクションの無いコンポは type 空文字 + 空 fields を返す
    [[nodiscard]] nlohmann::json SerializeComponent(const Component& comp);

    //! obj の全 component を SerializeComponent で {type, fields} の配列へ並べる
    //! 保存を data でなく実体から作る統合経路。 リフレクションの無い component は type 空文字で混ざる
    [[nodiscard]] nlohmann::json SerializeGameObjectComponents(const GameObject& obj);

    //! fields object の各キーをリフレクション FieldDesc に照合し、 一致する field を set で書き戻す
    //! 欠損キーは前方互換のため既定値のまま、 型不一致・未知キーは無視する
    void ApplyJsonFields(Component& comp, const nlohmann::json& fields);
} // namespace NS::Object
