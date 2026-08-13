#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace NS::Object
{
    class Component;

    //! リフレクションが扱うフィールド型タグ。Radians / enum は後続で追加する
    enum class FieldType
    {
        Float,
        Int,
        Bool,
        Vector3,
        String,
        ObjectRef
    };

    //! メンバ型から FieldType タグを引く。マクロが型タグを自動推論するのに使う。未対応型はここで弾く
    template <class T> constexpr FieldType FieldTypeOf() noexcept
    {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool> ||
                          std::is_same_v<T, NS::Core::Vector3> || std::is_same_v<T, std::string> ||
                          std::is_same_v<T, ObjectRef>,
                      "reflection: 未対応のフィールド型 (Float / Int / Bool / Vector3 / String / ObjectRef のみ)");
        if constexpr (std::is_same_v<T, float>)
            return FieldType::Float;
        else if constexpr (std::is_same_v<T, int>)
            return FieldType::Int;
        else if constexpr (std::is_same_v<T, bool>)
            return FieldType::Bool;
        else if constexpr (std::is_same_v<T, std::string>)
            return FieldType::String;
        else if constexpr (std::is_same_v<T, ObjectRef>)
            return FieldType::ObjectRef;
        else
            return FieldType::Vector3;
    }

    //! @brief リフレクションされた 1 フィールドの記述子
    //! @details get/set は型消去した関数ポインタ。obj はリフレクション対象そのものへの生ポインタで、
    //! Component でも素の値型でもよい。マクロが宣言時の具象型へ static_cast して読み書きする
    struct FieldDesc
    {
        const char* name;                                      // フィールド名
        FieldType type;                                        // 値の型タグ
        void (*get)(const void* obj, void* outValue) noexcept; // obj から値を outValue へ取り出す
        void (*set)(void* obj, const void* inValue) noexcept;  // inValue を obj へ書き込む
    };

    //! @brief 1 コンポーネント型のリフレクション情報。マクロで宣言したフィールドの名前 / 型 / get / set を束ねる
    //! @details エディタは Component* 越しに fields を列挙して編集 UI を自動生成する
    //! base は基底型のリフレクションを指し、辿る鎖で is-a も判定する
    //! 依存: NS::Core
    struct ReflectionInfo
    {
        const char* typeName;       // リフレクションする型名
        const FieldDesc* fields;    // フィールド記述子配列 (static 寿命)
        std::size_t fieldCount;     // fields の要素数
        const ReflectionInfo* base; // 基底型のリフレクション (Component 直下は鎖の終端 nullptr)
    };

    //! 基底型のリフレクションを返す。Component 直下は Component、素の値型は void を渡し、いずれも鎖の終端 nullptr になる
    template <class TBase> [[nodiscard]] const ReflectionInfo* ReflectionBaseOf() noexcept
    {
        if constexpr (std::is_same_v<TBase, Component> || std::is_same_v<TBase, void>)
            return nullptr;
        else
            return TBase::StaticReflection();
    }

    //! info->fields から name 一致の最初の 1 件を返す。無ければ nullptr、info が nullptr でも nullptr
    //! 基底鎖は辿らない。リフレクション欄は継承分も accessor で平坦に並べる約束に合わせる
    [[nodiscard]] inline const FieldDesc* FindField(const ReflectionInfo* info, std::string_view name) noexcept
    {
        if (info == nullptr)
        {
            return nullptr;
        }
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (name == info->fields[i].name)
            {
                return &info->fields[i];
            }
        }
        return nullptr;
    }
} // namespace NS::Object

//! 直メンバ用フィールド宣言の開始。クラス本体の public 節に、直接の基底型と並べて書く
#define NS_REFLECT_BEGIN(ThisType, BaseType)                                                                           \
    [[nodiscard]] static const NS::Object::ReflectionInfo* StaticReflection() noexcept                                \
    {                                                                                                                  \
        using Self = ThisType;                                                                                         \
        using ReflectBase = BaseType;                                                                                  \
        static constexpr const char* k_TypeName = #ThisType;                                                           \
        static const NS::Object::FieldDesc k_Fields[] = {

//! 同一クラスの直メンバを 1 フィールドとして登録する。private メンバも対象にできる。型タグはメンバ型から推論する
#define NS_REFLECT_FIELD(member, label)                                                                                \
    NS::Object::FieldDesc{label,                                                                                      \
                           NS::Object::FieldTypeOf<decltype(Self::member)>(),                                         \
                           +[](const void* c, void* out) noexcept {                                                    \
                               *static_cast<decltype(Self::member)*>(out) = static_cast<const Self*>(c)->member;       \
                           },                                                                                          \
                           +[](void* c, const void* in) noexcept {                                                     \
                               static_cast<Self*>(c)->member = *static_cast<const decltype(Self::member)*>(in);        \
                           }},

//! 基底の private や検証付きフィールドを getter/setter 経由で登録する。getter は値返し、setter は 1 引数
#define NS_REFLECT_ACCESSOR(ValueType, label, getterCall, setterCall)                                                  \
    NS::Object::FieldDesc{label,                                                                                      \
                           NS::Object::FieldTypeOf<ValueType>(),                                                      \
                           +[](const void* c, void* out) noexcept {                                                    \
                               *static_cast<ValueType*>(out) = static_cast<const Self*>(c)->getterCall;                \
                           },                                                                                          \
                           +[](void* c, const void* in) noexcept {                                                     \
                               static_cast<Self*>(c)->setterCall(*static_cast<const ValueType*>(in));                  \
                           }},

//! フィールド宣言の終了。static なリフレクション情報を組み立てて返し、仮想の GetReflection はそこへ転送する
#define NS_REFLECT_END()                                                                                               \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::Object::ReflectionInfo k_Info{                                                                   \
        k_TypeName, k_Fields, sizeof(k_Fields) / sizeof(k_Fields[0]), NS::Object::ReflectionBaseOf<ReflectBase>()};   \
    return &k_Info;                                                                                                    \
    }                                                                                                                  \
    [[nodiscard]] const NS::Object::ReflectionInfo* GetReflection() const noexcept override                           \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }

//! 値型用のフィールド宣言終了。Component を継承しない型向けに、仮想の GetReflection を出さず静的関数だけ定義する
#define NS_REFLECT_END_VALUE()                                                                                         \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::Object::ReflectionInfo k_Info{                                                                   \
        k_TypeName, k_Fields, sizeof(k_Fields) / sizeof(k_Fields[0]), NS::Object::ReflectionBaseOf<ReflectBase>()};   \
    return &k_Info;                                                                                                    \
    }

//! 調整フィールドを持たない型用。typeName と基底だけのリフレクション情報を返す。空配列は宣言できないため fields は nullptr
#define NS_REFLECT_NONE(ThisType, BaseType)                                                                            \
    [[nodiscard]] static const NS::Object::ReflectionInfo* StaticReflection() noexcept                                \
    {                                                                                                                  \
        static const NS::Object::ReflectionInfo k_Info{                                                               \
            #ThisType, nullptr, 0, NS::Object::ReflectionBaseOf<BaseType>()};                                         \
        return &k_Info;                                                                                                \
    }                                                                                                                  \
    [[nodiscard]] const NS::Object::ReflectionInfo* GetReflection() const noexcept override                           \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }
