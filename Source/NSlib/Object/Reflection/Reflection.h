#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Reflection/ActorRef.h"
#include "NSlib/Object/Reflection/Curve.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS::Obj
{
    class SubObject;

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
        ActorRef,
        Curve
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
        else if constexpr (std::is_same_v<T, NS::Quaternion>)
        {
            return FieldType::Quaternion;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return FieldType::String;
        }
        else if constexpr (std::is_same_v<T, ActorRef>)
        {
            return FieldType::ActorRef;
        }
        else if constexpr (std::is_same_v<T, Curve>)
        {
            return FieldType::Curve;
        }
        else
        {
            static_assert(std::is_same_v<T, NS::Vector3>,
                          "FieldTypeOf の T は float / int / bool / NS::Vector3 / NS::Quaternion / "
                          "std::string / ActorRef / Curve のいずれか");
            return FieldType::Vector3;
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
        else
        {
            target = *static_cast<const Field*>(in);
        }
    }

    //! @brief リフレクションされた 1 フィールドの記述子
    //! @details get/set は型消去した関数ポインタ。obj はリフレクション対象そのものへの生ポインタで、
    //! SubObject でも素の値型でもよい。マクロが宣言時の具象型へ static_cast して読み書きする
    struct FieldDesc
    {
        const char* name;                                      // フィールド名
        FieldType type;                                        // 値の型タグ
        void (*get)(const void* obj, void* outValue) noexcept; // obj から値を outValue へ取り出す
        void (*set)(void* obj, const void* inValue) noexcept;  // inValue を obj へ書き込む
    };

    //! @brief 欄の見出し 1 つ。インスペクタが firstField の欄の前に畳める区切りを出す
    //! @details 見出しは次の見出しの手前の欄までを束ねる。次の見出しと firstField が同じなら欄を持たない
    struct FieldGroup
    {
        const char* name;       // 見出しの名前
        std::size_t firstField; // この見出しの最初の欄の、fields の添字
    };

    //! @brief 1 サブオブジェクト型のリフレクション情報。マクロで宣言したフィールドの名前 / 型 / get / set を束ねる
    //! @details エディタは SubObject* 越しに fields を列挙して編集 UI を自動生成する
    //! base は基底型のリフレクションを指し、SubObject::IsA はこれを辿って継承関係を判定する。
    //! 見出しは fields に混ざらず groups に別に並ぶので、保存と読み込みは見出しを見ない
    //! 依存: NS
    struct ReflectionInfo
    {
        const char* typeName;               // リフレクションする型名
        const FieldDesc* fields;            // フィールド記述子配列 (static 寿命)
        std::size_t fieldCount;             // fields の要素数
        const ReflectionInfo* base;         // 基底型のリフレクション。SubObject 直下は nullptr
        const FieldGroup* groups = nullptr; // 欄の見出しの配列 (static 寿命)。宣言の並び
        std::size_t groupCount = 0;         // groups の要素数
    };

    //! @brief マクロが宣言の並びのまま積む 1 項目。欄か見出しのどちらか 1 つ
    struct ReflectEntry
    {
        FieldDesc field;   // 欄。見出しの項目では空
        const char* group; // 見出しの名前。欄の項目では nullptr
    };

    //! @brief 宣言の並びを、欄の配列と見出しの配列に分けて持つ
    //! @details 型ごとに StaticReflection の中で 1 つだけ作り、生涯書き換えない。ReflectionInfo は中の配列を指すので
    //! 写しを作らない
    class ReflectedFields
    {
    public:
        //! @param[in] entries 欄と見出しを宣言の並びのまま並べた物
        explicit ReflectedFields(std::span<const ReflectEntry> entries) noexcept
        {
            for (const ReflectEntry& entry : entries)
            {
                if (entry.group != nullptr)
                {
                    m_groups.push_back(FieldGroup{entry.group, m_fields.size()});
                }
                else
                {
                    m_fields.push_back(entry.field);
                }
            }
        }
        ReflectedFields(const ReflectedFields&) = delete;
        ReflectedFields& operator=(const ReflectedFields&) = delete;

        //! @brief 分けた配列を指すリフレクション情報を返す
        //! @param[in] typeName リフレクションする型名
        //! @param[in] base 基底型のリフレクション。無ければ nullptr
        //! @return 欄と見出しを指すリフレクション情報。指す先はこの物が生きている間だけ有効
        [[nodiscard]] ReflectionInfo MakeInfo(const char* typeName, const ReflectionInfo* base) const noexcept
        {
            return ReflectionInfo{typeName, m_fields.data(), m_fields.size(), base, m_groups.data(), m_groups.size()};
        }

    private:
        std::vector<FieldDesc> m_fields;  // 見出しを除いた欄。宣言の並び
        std::vector<FieldGroup> m_groups; // 見出し。宣言の並び
    };

    //! 基底型のリフレクションを返す。SubObject 直下は SubObject、素の値型は void を渡し、いずれも nullptr になる
    template <class TBase> [[nodiscard]] const ReflectionInfo* ReflectionBaseOf() noexcept
    {
        if constexpr (std::is_same_v<TBase, void>)
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
    [[nodiscard]] static const NS::Obj::ReflectionInfo* StaticReflection() noexcept                                    \
    {                                                                                                                  \
        using Self = ThisType;                                                                                         \
        using ReflectBase = BaseType;                                                                                  \
        static constexpr const char* k_TypeName = #ThisType;                                                           \
        static const NS::Obj::ReflectEntry k_Entries[] = {

//! メンバを 1 フィールドとして登録する。private も入れ子の struct のメンバ (m_tuning.speed) も書ける
//! 型タグはメンバ型から推論する。float の欄は AssignIfFinite を通るので、非有限値の書き込みは捨てて元の値が残る
#define NS_REFLECT_FIELD(member, label)                                                                                \
    NS::Obj::ReflectEntry{NS::Obj::FieldDesc{label,                                                                    \
                                             NS::Obj::FieldTypeOf<decltype(Self::member)>(),                           \
                                             +[](const void* c, void* out) noexcept {                                  \
                                                 *static_cast<decltype(Self::member)*>(out) =                          \
                                                     static_cast<const Self*>(c)->member;                              \
                                             },                                                                        \
                                             +[](void* c, const void* in) noexcept {                                   \
                                                 NS::Obj::AssignFieldValue(static_cast<Self*>(c)->member, in);         \
                                             }},                                                                       \
                          nullptr},

//! 欄の見出しを置く。次の見出しまでに並べた欄がこの見出しの下に入る。見出しは欄の配列に入らず、保存の鍵にならない
#define NS_REFLECT_GROUP(groupName) NS::Obj::ReflectEntry{NS::Obj::FieldDesc{}, groupName},

//! 基底の private や検証付きフィールドを getter/setter 経由で登録する。getter は値返し、setter は 1 引数
#define NS_REFLECT_ACCESSOR(ValueType, label, getterCall, setterCall)                                                  \
    NS::Obj::ReflectEntry{                                                                                             \
        NS::Obj::FieldDesc{label,                                                                                      \
                           NS::Obj::FieldTypeOf<ValueType>(),                                                          \
                           +[](const void* c, void* out) noexcept {                                                    \
                               *static_cast<ValueType*>(out) = static_cast<const Self*>(c)->getterCall;                \
                           },                                                                                          \
                           +[](void* c, const void* in) noexcept {                                                     \
                               static_cast<Self*>(c)->setterCall(*static_cast<const ValueType*>(in));                  \
                           }},                                                                                         \
        nullptr},

//! フィールド宣言の終了。static なリフレクション情報を組み立てて返し、仮想の GetReflection はそこへ転送する
#define NS_REFLECT_END()                                                                                               \
    NS_REFLECT_END_VALUE()                                                                                             \
    [[nodiscard]] const NS::Obj::ReflectionInfo* GetReflection() const noexcept override                               \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }

//! 値型用のフィールド宣言終了。SubObject を継承しない型向けに、仮想の GetReflection を出さず静的関数だけ定義する
#define NS_REFLECT_END_VALUE()                                                                                         \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::Obj::ReflectedFields k_Fields{k_Entries};                                                         \
    static const NS::Obj::ReflectionInfo k_Info =                                                                      \
        k_Fields.MakeInfo(k_TypeName, NS::Obj::ReflectionBaseOf<ReflectBase>());                                       \
    return &k_Info;                                                                                                    \
    }

//! 調整フィールドを持たない型用。typeName と基底だけのリフレクション情報を返す。空配列は宣言できないため fields は
//! nullptr
#define NS_REFLECT_NONE(ThisType, BaseType)                                                                            \
    [[nodiscard]] static const NS::Obj::ReflectionInfo* StaticReflection() noexcept                                    \
    {                                                                                                                  \
        static const NS::Obj::ReflectionInfo k_Info{#ThisType, nullptr, 0, NS::Obj::ReflectionBaseOf<BaseType>()};     \
        return &k_Info;                                                                                                \
    }                                                                                                                  \
    [[nodiscard]] const NS::Obj::ReflectionInfo* GetReflection() const noexcept override                               \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }
