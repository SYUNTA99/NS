#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"
#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS
{
    class Object;

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
        Curve,
        AssetRef // 欄は AssetRef<T>。取得・設定の関数が受け渡すのは名前の std::string
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
        else if constexpr (std::is_same_v<T, Quaternion>)
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
        else if constexpr (IsAssetRef<T>::value)
        {
            return FieldType::AssetRef;
        }
        else
        {
            static_assert(std::is_same_v<T, Vector3>,
                          "FieldTypeOf の T は float / int / bool / NS::Vector3 / NS::Quaternion / "
                          "std::string / ActorRef / Curve / AssetRef<T> のいずれか");
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

    // 捨てる枝の型検査を避けるため、マクロの外のテンプレートで分岐する
    template <class Field> void ReadFieldValue(const Field& source, void* out) noexcept
    {
        if constexpr (IsAssetRef<Field>::value)
        {
            *static_cast<std::string*>(out) = source.Name();
        }
        else
        {
            *static_cast<Field*>(out) = source;
        }
    }

    template <class Field> void LoadFieldAsset(Field& field)
    {
        if constexpr (IsAssetRef<Field>::value)
        {
            LoadAssetRef(field);
        }
    }

    template <class Field> void AssignFieldValue(Field& target, const void* in) noexcept
    {
        if constexpr (std::is_same_v<Field, float>)
        {
            AssignIfFinite(target, *static_cast<const float*>(in));
        }
        else if constexpr (IsAssetRef<Field>::value)
        {
            target.SetName(*static_cast<const std::string*>(in));
        }
        else
        {
            target = *static_cast<const Field*>(in);
        }
    }

    //! 型消去したフィールドの取得・設定関数。obj は宣言した具象型を指す
    struct FieldDesc
    {
        const char* name;
        FieldType type;
        void (*get)(const void* obj, void* outValue) noexcept; // obj から値を outValue へ取り出す
        void (*set)(void* obj, const void* inValue) noexcept;  // inValue を obj へ書き込む
        void (*load)(void* obj) = nullptr;                    // 参照の欄の素材を読む。ほかの型の欄では何もしない
    };

    //! @brief 欄の見出し 1 つ。インスペクタが firstField の欄の前に畳める区切りを出す
    //! @details 見出しは次の見出しの手前の欄までを束ねる。次の見出しと firstField が同じなら欄を持たない
    struct FieldGroup
    {
        const char* name;
        std::size_t firstField; // この見出しの最初の欄の、fields の添字
    };

    // 保存キーと固定メンバーへのアクセス。所有や部品の交換は行わない
    struct SubObjectDesc
    {
        const char* key;
        Object* (*get)(const Object* owner) noexcept;
        const void* (*member)(const Object* owner) noexcept;
    };

    // 型ごとの不変な情報。値の欄・見出し・部品メンバーは別々に保持する
    struct ReflectionInfo
    {
        const char* typeName;
        const FieldDesc* fields; // フィールド記述子配列 (static 寿命)
        std::size_t fieldCount;
        const ReflectionInfo* base;         // 基底型のリフレクション。Object と素の値型は nullptr
        const FieldGroup* groups = nullptr; // 欄の見出しの配列 (static 寿命)。宣言の並び
        std::size_t groupCount = 0;
        const SubObjectDesc* subObjects = nullptr;
        std::size_t subObjectCount = 0;
    };

    // マクロが宣言順に積む、値の欄・見出し・部品メンバーのいずれか
    struct ReflectEntry
    {
        FieldDesc field;   // 欄。見出しの項目では空
        const char* group; // 見出しの名前。欄の項目では nullptr
        SubObjectDesc subObject{};
    };

    //! 型ごとの不変なフィールド配列。ReflectionInfo が参照するためコピーしない
    class ReflectedFields
    {
    public:
        explicit ReflectedFields(std::span<const ReflectEntry> entries) noexcept
        {
            for (const ReflectEntry& entry : entries)
            {
                if (entry.subObject.key != nullptr)
                {
                    m_subObjects.push_back(entry.subObject);
                }
                else if (entry.group != nullptr)
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

        //! 戻り値の配列参照は、このオブジェクトが生存している間だけ有効
        [[nodiscard]] ReflectionInfo MakeInfo(const char* typeName, const ReflectionInfo* base) const noexcept
        {
            return ReflectionInfo{typeName,
                                  m_fields.data(),
                                  m_fields.size(),
                                  base,
                                  m_groups.data(),
                                  m_groups.size(),
                                  m_subObjects.data(),
                                  m_subObjects.size()};
        }

    private:
        std::vector<SubObjectDesc> m_subObjects;
        std::vector<FieldDesc> m_fields;  // 見出しを除いた欄。宣言の並び
        std::vector<FieldGroup> m_groups; // 見出し。宣言の並び
    };

    //! 基底型のリフレクションを返す。素の値型は void を渡し、nullptr になる
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
} // namespace NS

//! フィールド宣言の開始。クラス本体の public 節に、自分の型と直接の基底型を並べて書く
#define NS_REFLECT_BEGIN(ThisType, BaseType)                                                                           \
    [[nodiscard]] static const NS::ReflectionInfo* StaticReflection() noexcept                                         \
    {                                                                                                                  \
        using Self = ThisType;                                                                                         \
        using ReflectBase = BaseType;                                                                                  \
        static constexpr const char* k_TypeName = #ThisType;                                                           \
        static const NS::ReflectEntry k_Entries[] = {

//! メンバを 1 フィールドとして登録する。private も入れ子の struct のメンバ (m_tuning.speed) も書ける
//! 型タグはメンバ型から推論する。float の欄は AssignIfFinite を通るので、非有限値の書き込みは捨てて元の値が残る
#define NS_REFLECT_FIELD(member, label)                                                                                \
    NS::ReflectEntry{NS::FieldDesc{label,                                                                              \
                                   NS::FieldTypeOf<decltype(Self::member)>(),                                          \
                                   +[](const void* c, void* out) noexcept {                                            \
                                       NS::ReadFieldValue(static_cast<const Self*>(c)->member, out);                   \
                                   },                                                                                  \
                                   +[](void* c, const void* in) noexcept {                                             \
                                       NS::AssignFieldValue(static_cast<Self*>(c)->member, in);                        \
                                   },                                                                                  \
                                   +[](void* c) { NS::LoadFieldAsset(static_cast<Self*>(c)->member); }},               \
                     nullptr},

//! 欄の見出しを置く。次の見出しまでに並べた欄がこの見出しの下に入る。見出しは欄の配列に入らず、保存の鍵にならない
#define NS_REFLECT_GROUP(groupName) NS::ReflectEntry{NS::FieldDesc{}, groupName},

//! 基底の private や検証付きフィールドを getter/setter 経由で登録する。getter は値返し、setter は 1 引数
#define NS_REFLECT_ACCESSOR(ValueType, label, getterCall, setterCall)                                                  \
    NS::ReflectEntry{NS::FieldDesc{label,                                                                              \
                                   NS::FieldTypeOf<ValueType>(),                                                       \
                                   +[](const void* c, void* out) noexcept {                                            \
                                       *static_cast<ValueType*>(out) = static_cast<const Self*>(c)->getterCall;        \
                                   },                                                                                  \
                                   +[](void* c, const void* in) noexcept {                                             \
                                       static_cast<Self*>(c)->setterCall(*static_cast<const ValueType*>(in));          \
                                   }},                                                                                 \
                     nullptr},

//! フィールド宣言の終了。static なリフレクション情報を組み立てて返し、仮想の GetReflection はそこへ転送する
#define NS_REFLECT_END()                                                                                               \
    NS_REFLECT_END_VALUE()                                                                                             \
    [[nodiscard]] const NS::ReflectionInfo* GetReflection() const noexcept override                                    \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }

// 保存キーは既存データとの対応に使う。型が同じ部品もメンバーごとに区別する
#define NS_REFLECT_SUBOBJ(memberName, savedKey)                                                                        \
    NS::ReflectEntry{NS::FieldDesc{},                                                                                  \
                     nullptr,                                                                                          \
                     NS::SubObjectDesc{savedKey,                                                                       \
                                       +[](const NS::Object* owner) noexcept -> NS::Object* {                       \
                                           return static_cast<const Self*>(owner)->memberName.get();                   \
                                       },                                                                              \
                                       +[](const NS::Object* owner) noexcept -> const void* {                          \
                                           return &static_cast<const Self*>(owner)->memberName;                        \
                                       }}},

// 部品の型を前方宣言するクラスでは、情報の定義を実装ファイルへ置く
#define NS_REFLECT_DECLARE()                                                                                           \
    [[nodiscard]] static const NS::ReflectionInfo* StaticReflection() noexcept;                                        \
    [[nodiscard]] const NS::ReflectionInfo* GetReflection() const noexcept override                                    \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }

#define NS_REFLECT_BEGIN_IMPL(ThisType, BaseType)                                                                      \
    const NS::ReflectionInfo* ThisType::StaticReflection() noexcept                                                    \
    {                                                                                                                  \
        using Self = ThisType;                                                                                         \
        using ReflectBase = BaseType;                                                                                  \
        static constexpr const char* k_TypeName = #ThisType;                                                           \
        static const NS::ReflectEntry k_Entries[] = {

// NS_REFLECT_BEGIN_IMPL の終了。仮想の GetReflection は NS_REFLECT_DECLARE が宣言に置く
#define NS_REFLECT_END_IMPL() NS_REFLECT_END_VALUE()

//! 値型用のフィールド宣言終了。Object を継承しない型向けに、仮想の GetReflection を出さず静的関数だけ定義する
#define NS_REFLECT_END_VALUE()                                                                                         \
    }                                                                                                                  \
    ;                                                                                                                  \
    static const NS::ReflectedFields k_Fields{k_Entries};                                                              \
    static const NS::ReflectionInfo k_Info = k_Fields.MakeInfo(k_TypeName, NS::ReflectionBaseOf<ReflectBase>());       \
    return &k_Info;                                                                                                    \
    }

//! 調整フィールドを持たない型用。typeName と基底だけのリフレクション情報を返す。空配列は宣言できないため fields は
//! nullptr
#define NS_REFLECT_NONE(ThisType, BaseType)                                                                            \
    [[nodiscard]] static const NS::ReflectionInfo* StaticReflection() noexcept                                         \
    {                                                                                                                  \
        static const NS::ReflectionInfo k_Info{#ThisType, nullptr, 0, NS::ReflectionBaseOf<BaseType>()};               \
        return &k_Info;                                                                                                \
    }                                                                                                                  \
    [[nodiscard]] const NS::ReflectionInfo* GetReflection() const noexcept override                                    \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }
