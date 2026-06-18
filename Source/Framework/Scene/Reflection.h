#pragma once

/// @file Reflection.h
/// @brief NS::Scene 反射コア — Component のフィールドをマクロ宣言し 名前 / 型 / get / set を公開する
///
/// @details `NS_REFLECT_BEGIN/FIELD/ACCESSOR/END` をクラス本体に書くと、その型の
/// `GetReflection()` が field 記述子配列を返すようになる。エディタはこれを `Component*` 越しに
/// 列挙して編集 UI を自動生成する。get/set は型消去した関数ポインタで、Component が仮想関数を
/// 持ち offsetof を使えないため static_cast で実装する。型タグはメンバ型から推論し、未対応型は
/// static_assert で弾く
/// 依存: NS::Math

#include "Framework/Math/Math.h"

#include <cstddef>
#include <type_traits>

namespace NS::Scene
{
    class Component;

    /// 反射が扱うフィールド型タグ。Radians / enum は後続で追加する
    enum class FieldType
    {
        Float,
        Int,
        Bool,
        Vector3
    };

    /// メンバ型 → FieldType タグの写像。マクロが型タグを自動推論するのに使う (未対応型はここで弾く)
    template <class T> constexpr FieldType FieldTypeOf() noexcept
    {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool> ||
                          std::is_same_v<T, NS::Math::Vector3>,
                      "reflection: 未対応のフィールド型 (Float / Int / Bool / Vector3 のみ)");
        if constexpr (std::is_same_v<T, float>)
            return FieldType::Float;
        else if constexpr (std::is_same_v<T, int>)
            return FieldType::Int;
        else if constexpr (std::is_same_v<T, bool>)
            return FieldType::Bool;
        else
            return FieldType::Vector3;
    }

    /// 反射された 1 フィールドの記述子。get/set は型消去 thunk、out/in はフィールド型の値を指す
    struct FieldDesc
    {
        const char* name;
        FieldType type;
        void (*get)(const Component* comp, void* outValue) noexcept;
        void (*set)(Component* comp, const void* inValue) noexcept;
    };

    /// 1 コンポーネント型の反射情報。fields は static 寿命の配列を指す
    struct ReflectionInfo
    {
        const char* typeName;
        const FieldDesc* fields;
        std::size_t fieldCount;
    };
} // namespace NS::Scene

/// 直メンバ用フィールド宣言の開始。クラス本体に書く
#define NS_REFLECT_BEGIN(ThisType)                                                                                     \
    [[nodiscard]] const NS::Scene::ReflectionInfo* GetReflection() const noexcept override                             \
    {                                                                                                                  \
        using Self = ThisType;                                                                                         \
        static constexpr const char* kTypeName = #ThisType;                                                            \
        static const NS::Scene::FieldDesc kFields[] = {

/// 同一クラスの (private 可) 直メンバを 1 フィールドとして登録する。型タグはメンバ型から推論する
#define NS_REFLECT_FIELD(member, label)                                                                                \
    NS::Scene::FieldDesc{label,                                                                                        \
                         NS::Scene::FieldTypeOf<decltype(Self::member)>(),                                             \
                         +[](const NS::Scene::Component* c, void* out) noexcept {                                      \
                             *static_cast<decltype(Self::member)*>(out) = static_cast<const Self*>(c)->member;         \
                         },                                                                                            \
                         +[](NS::Scene::Component* c, const void* in) noexcept {                                       \
                             static_cast<Self*>(c)->member = *static_cast<const decltype(Self::member)*>(in);          \
                         }},

/// 基底の private や検証付きフィールドを getter/setter 経由で登録する (getter は値返し、setter は 1 引数)
#define NS_REFLECT_ACCESSOR(ValueType, label, getterCall, setterCall)                                                  \
    NS::Scene::FieldDesc{label,                                                                                        \
                         NS::Scene::FieldTypeOf<ValueType>(),                                                          \
                         +[](const NS::Scene::Component* c, void* out) noexcept {                                      \
                             *static_cast<ValueType*>(out) = static_cast<const Self*>(c)->getterCall;                  \
                         },                                                                                            \
                         +[](NS::Scene::Component* c, const void* in) noexcept {                                       \
                             static_cast<Self*>(c)->setterCall(*static_cast<const ValueType*>(in));                    \
                         }},

/// フィールド宣言の終了。反射情報を組み立てて返す
#define NS_REFLECT_END()                                                                                               \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::Scene::ReflectionInfo kInfo{kTypeName, kFields, sizeof(kFields) / sizeof(kFields[0])};            \
    return &kInfo;                                                                                                     \
    }
