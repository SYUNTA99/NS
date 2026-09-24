#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/ComponentRef.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace NS::Obj
{
    class Component;

    //! リフレクションが扱うフィールド型タグ
    // TODO: 角度の欄は素の float / Vector3 で、単位は欄名だけが持つ。Core/Math.h の Radians を FieldType へ足す
    enum class FieldType
    {
        Float,
        Int,
        Bool,
        Vector3,
        Quaternion,
        String,
        ObjectRef,
        Curve,
        ComponentRef
    };

    //! メンバ型から FieldType タグを引く。マクロが型タグを自動推論するのに使う。未対応型はここで弾く
    template <class T> constexpr FieldType FieldTypeOf() noexcept
    {
        if constexpr (std::is_same_v<T, float>)
        {
            return FieldType::Float;
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            return FieldType::Int;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return FieldType::Bool;
        }
        else if constexpr (std::is_same_v<T, NS::Core::Quaternion>)
        {
            return FieldType::Quaternion;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return FieldType::String;
        }
        else if constexpr (std::is_same_v<T, ObjectRef>)
        {
            return FieldType::ObjectRef;
        }
        else if constexpr (std::is_same_v<T, Curve>)
        {
            return FieldType::Curve;
        }
        else if constexpr (std::is_base_of_v<ComponentRefValue, T>)
        {
            return FieldType::ComponentRef;
        }
        else
        {
            static_assert(std::is_same_v<T, NS::Core::Vector3>,
                          "FieldTypeOf の T は float / int / bool / NS::Core::Vector3 / NS::Core::Quaternion / "
                          "std::string / ObjectRef / Curve / ComponentRef<T> のいずれか");
            return FieldType::Vector3;
        }
    }

    //! @brief 型消去した get / set が値を受け渡す型。ComponentRef<T> は型を問わない ComponentRefValue で渡す
    //! @details 保存と Inspector は欄の T を知らないので、共通の形で読み書きする
    template <class T>
    using FieldStorageOf = std::conditional_t<std::is_base_of_v<ComponentRefValue, T>, ComponentRefValue, T>;

    //! 型のリフレクション情報を返す関数。ComponentRef<T> の欄が T::StaticReflection を持つのに使う
    using ReflectionInfoFn = const ReflectionInfo* (*)() noexcept;

    //! ComponentRef<T> の欄なら T のリフレクションを返す関数を、それ以外は nullptr を返す
    template <class T> constexpr ReflectionInfoFn FieldRefTypeOf() noexcept
    {
        if constexpr (requires { typename T::Target; })
        {
            return &T::Target::StaticReflection;
        }
        else
        {
            return nullptr;
        }
    }

    //! 有限値のときだけ target へ書く。非有限値は捨てて元の値を残す
    inline void AssignIfFinite(float& target, float value) noexcept
    {
        if (std::isfinite(value))
        {
            target = value;
        }
    }

    // テンプレートの外の if constexpr は捨てる枝も検査される
    // マクロへ直に書くと clang が Vector3 の欄で AssignIfFinite(Vector3&, float) を型エラーにする
    //! in の値を target へ書く。Field が float のときだけ AssignIfFinite を通し、他はそのまま代入する
    template <class Field> void AssignFieldValue(Field& target, const void* in) noexcept
    {
        if constexpr (std::is_same_v<Field, float>)
        {
            AssignIfFinite(target, *static_cast<const float*>(in));
        }
        else if constexpr (std::is_base_of_v<ComponentRefValue, Field>)
        {
            static_cast<ComponentRefValue&>(target) = *static_cast<const ComponentRefValue*>(in);
        }
        else
        {
            target = *static_cast<const Field*>(in);
        }
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
        ReflectionInfoFn refType = nullptr; // ComponentRef<T> の欄だけ T のリフレクション。選べる相手を絞る
    };

    //! @brief 1 コンポーネント型のリフレクション情報。マクロで宣言したフィールドの名前 / 型 / get / set を束ねる
    //! @details エディタは Component* 越しに fields を列挙して編集 UI を自動生成する
    //! base は基底型のリフレクションを指し、Component::IsA はこれを辿って継承関係を判定する
    //! 依存: NS::Core
    struct ReflectionInfo
    {
        const char* typeName;       // リフレクションする型名
        const FieldDesc* fields;    // フィールド記述子配列 (static 寿命)
        std::size_t fieldCount;     // fields の要素数
        const ReflectionInfo* base; // 基底型のリフレクション。Component 直下は nullptr
    };

    //! 基底型のリフレクションを返す。Component 直下は Component、素の値型は void を渡し、いずれも nullptr になる
    template <class TBase> [[nodiscard]] const ReflectionInfo* ReflectionBaseOf() noexcept
    {
        if constexpr (std::is_same_v<TBase, Component> || std::is_same_v<TBase, void>)
        {
            return nullptr;
        }
        else
        {
            return TBase::StaticReflection();
        }
    }

    //! info->fields から name 一致の最初の 1 件を返す。無ければ nullptr、info が nullptr でも nullptr
    //! 基底型のリフレクションは辿らない。リフレクション欄は継承分も accessor で平坦に並べる約束に合わせる
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
} // namespace NS::Obj

//! フィールド宣言の開始。クラス本体の public 節に、自分の型と直接の基底型を並べて書く
#define NS_REFLECT_BEGIN(ThisType, BaseType)                                                                           \
    [[nodiscard]] static const NS::Obj::ReflectionInfo* StaticReflection() noexcept                                 \
    {                                                                                                                  \
        using Self = ThisType;                                                                                         \
        using ReflectBase = BaseType;                                                                                  \
        static constexpr const char* k_TypeName = #ThisType;                                                           \
        static const NS::Obj::FieldDesc k_Fields[] = {

//! メンバを 1 フィールドとして登録する。private も入れ子の struct のメンバ (m_tuning.speed) も書ける
//! 型タグはメンバ型から推論する。float の欄は AssignIfFinite を通るので、非有限値の書き込みは捨てて元の値が残る
#define NS_REFLECT_FIELD(member, label)                                                                                \
    NS::Obj::FieldDesc{                                                                                             \
        label,                                                                                                         \
        NS::Obj::FieldTypeOf<decltype(Self::member)>(),                                                             \
        +[](const void* c, void* out) noexcept {                                                                       \
            *static_cast<NS::Obj::FieldStorageOf<decltype(Self::member)>*>(out) = static_cast<const Self*>(c)->member; \
        },                                                                                                             \
        +[](void* c, const void* in) noexcept { NS::Obj::AssignFieldValue(static_cast<Self*>(c)->member, in); },      \
        NS::Obj::FieldRefTypeOf<decltype(Self::member)>()},

//! 基底の private や検証付きフィールドを getter/setter 経由で登録する。getter は値返し、setter は 1 引数
#define NS_REFLECT_ACCESSOR(ValueType, label, getterCall, setterCall)                                                  \
    NS::Obj::FieldDesc{label,                                                                                       \
                          NS::Obj::FieldTypeOf<ValueType>(),                                                        \
                          +[](const void* c, void* out) noexcept {                                                     \
                              *static_cast<ValueType*>(out) = static_cast<const Self*>(c)->getterCall;                 \
                          },                                                                                           \
                          +[](void* c, const void* in) noexcept {                                                      \
                              static_cast<Self*>(c)->setterCall(*static_cast<const ValueType*>(in));                   \
                          }},

//! フィールド宣言の終了。static なリフレクション情報を組み立てて返し、仮想の GetReflection はそこへ転送する
#define NS_REFLECT_END()                                                                                               \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::Obj::ReflectionInfo k_Info{                                                                    \
        k_TypeName, k_Fields, sizeof(k_Fields) / sizeof(k_Fields[0]), NS::Obj::ReflectionBaseOf<ReflectBase>()};    \
    return &k_Info;                                                                                                    \
    }                                                                                                                  \
    [[nodiscard]] const NS::Obj::ReflectionInfo* GetReflection() const noexcept override                            \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }

//! 値型用のフィールド宣言終了。Component を継承しない型向けに、仮想の GetReflection を出さず静的関数だけ定義する
#define NS_REFLECT_END_VALUE()                                                                                         \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::Obj::ReflectionInfo k_Info{                                                                    \
        k_TypeName, k_Fields, sizeof(k_Fields) / sizeof(k_Fields[0]), NS::Obj::ReflectionBaseOf<ReflectBase>()};    \
    return &k_Info;                                                                                                    \
    }

//! 調整フィールドを持たない型用。typeName と基底だけのリフレクション情報を返す。空配列は宣言できないため fields は
//! nullptr
#define NS_REFLECT_NONE(ThisType, BaseType)                                                                            \
    [[nodiscard]] static const NS::Obj::ReflectionInfo* StaticReflection() noexcept                                 \
    {                                                                                                                  \
        static const NS::Obj::ReflectionInfo k_Info{                                                                \
            #ThisType, nullptr, 0, NS::Obj::ReflectionBaseOf<BaseType>()};                                          \
        return &k_Info;                                                                                                \
    }                                                                                                                  \
    [[nodiscard]] const NS::Obj::ReflectionInfo* GetReflection() const noexcept override                            \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }
