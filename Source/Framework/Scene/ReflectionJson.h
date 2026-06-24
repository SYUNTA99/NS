#pragma once

/// @file ReflectionJson.h
/// @brief 反射駆動で Component を {type, fields} JSON へ往復させる glue
///
/// @details Component の GetReflection() が返す FieldDesc 列を辿り、 各 field を nlohmann::json
/// へ書き出す / JSON から set で書き戻す。 InspectorReflection の編集 switch と同型の write/read を
/// 1 箇所へ集約する。 値の境界は文字列ではなく nlohmann::json を直接受け渡すので、 呼出側 (level
/// loader) は object 単位の配列へそのまま push できる
/// 依存: NS::Scene::Component / Reflection、 nlohmann::json

#include "Framework/Scene/Component.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Scene
{
    /// comp を {"type": 反射 typeName, "fields": {名前: 値}} の JSON object へ書き出す
    /// 反射が無い (GetReflection() == nullptr) コンポは type 空文字 + 空 fields を返す
    [[nodiscard]] nlohmann::json SerializeComponent(const Component& comp);

    /// fields object の各キーを反射 FieldDesc に照合し、 一致する field を set で書き戻す
    /// 欠損キーは既定値のまま (前方互換)、 型不一致・未知キーは無視する
    void ApplyJsonFields(Component& comp, const nlohmann::json& fields);
} // namespace NS::Scene
