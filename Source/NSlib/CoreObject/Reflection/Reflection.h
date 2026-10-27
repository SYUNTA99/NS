#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/EffectName.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS
{
    class Object;

    // 保存形式を変えず、欄の参照先の種類に合う素材を選ぶための情報
    enum class AssetKind
    {
        None,
        Mesh,
        Material,
        Model,
        Animation,
        Effect,
        Sound,
    };

    template <class T> constexpr AssetKind AssetKindOf() noexcept
    {
        if constexpr (std::is_same_v<T, Mesh*>)
        {
            return AssetKind::Mesh;
        }
        else if constexpr (std::is_same_v<T, Material*>)
        {
            return AssetKind::Material;
        }
        else if constexpr (std::is_same_v<T, SkinnedModel*>)
        {
            return AssetKind::Model;
        }
        else if constexpr (std::is_same_v<T, AnimClip*>)
        {
            return AssetKind::Animation;
        }
        else if constexpr (std::is_same_v<T, EffectName>)
        {
            return AssetKind::Effect;
        }
        else if constexpr (std::is_same_v<T, SoundName>)
        {
            return AssetKind::Sound;
        }
        return AssetKind::None;
    }

    //! リフレクションが扱うフィールド型タグ
    // TODO: 角度の欄は素の float / Vector3 で、単位は欄名だけが持つ。Core/Math.h の Radians を FieldType へ足す
    enum class FieldType
    {
        Float,
        Int,
        Bool,
        Vector3,
        Quaternion,
        Color,  // 欄は NS::Color。保存は [r,g,b,a] の 4 成分
        String, // 欄は std::string か EffectName か SoundName。取得・設定の関数が受け渡すのは std::string
        ActorRef,
        Curve,
        Asset, // 欄は素材の姿のポインタ。取得・設定の関数が受け渡すのは道の std::string
        Vector2,
        OptionalColor // 欄は std::optional<NS::Color>。空は null、値は Color と同じ [r,g,b,a] で保存する
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
        else if constexpr (std::is_same_v<T, Vector2>)
        {
            return FieldType::Vector2;
        }
        else if constexpr (std::is_same_v<T, Quaternion>)
        {
            return FieldType::Quaternion;
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            return FieldType::Color;
        }
        else if constexpr (std::is_same_v<T, std::optional<Color>>)
        {
            return FieldType::OptionalColor;
        }
        else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, EffectName> || std::is_same_v<T, SoundName>)
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
        else if constexpr (IsAssetPointer<T>::value)
        {
            return FieldType::Asset;
        }
        else
        {
            static_assert(
                std::is_same_v<T, Vector3>,
                "FieldTypeOf の T は float / int / bool / NS::Vector2 / NS::Vector3 / NS::Quaternion / NS::Color / "
                "std::optional<NS::Color> / std::string / EffectName / SoundName / ActorRef / Curve / IsAssetPointer に並ぶ素材の姿のポインタ のいずれか");
            return FieldType::Vector3;
        }
    }

    //! 欄へ書く値の float の成分が全部有限なら true。float を持たない型は常に true
    template <class Value> [[nodiscard]] bool IsFiniteFieldValue(const Value& value) noexcept
    {
        if constexpr (std::is_same_v<Value, float>)
        {
            return std::isfinite(value);
        }
        else if constexpr (std::is_same_v<Value, Vector2>)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }
        else if constexpr (std::is_same_v<Value, Vector3>)
        {
            return IsFinite(value);
        }
        else if constexpr (std::is_same_v<Value, Quaternion> || std::is_same_v<Value, Color>)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
        }
        else if constexpr (std::is_same_v<Value, std::optional<Color>>)
        {
            return !value.has_value() || IsFiniteFieldValue(*value);
        }
        else if constexpr (std::is_same_v<Value, Curve>)
        {
            // 使う先頭 count 個だけを見る。count が上限を超えても配列の外は読まない
            for (uint32 i = 0; i < value.count && i < Curve::k_MaxKeys; ++i)
            {
                if (!value.keys[i].IsFinite())
                {
                    return false;
                }
            }
            return true;
        }
        else
        {
            return true;
        }
    }

    //! @brief 調整値が取ってよい値の範囲。両端を含む
    //! @details 登録のマクロの付け足しの引数に書く。範囲を付けられるのは float の欄だけ
    struct FieldRange
    {
        float min = -std::numeric_limits<float>::max();
        float max = std::numeric_limits<float>::max();

        //! k_Epsilon 以上の値。0 と、割る数にすると溢れるほど 0 に近い正の値は範囲の外
        [[nodiscard]] static constexpr FieldRange Positive() noexcept
        {
            return FieldRange{k_Epsilon, std::numeric_limits<float>::max()};
        }
        //! 0 以上の値
        [[nodiscard]] static constexpr FieldRange NonNegative() noexcept
        {
            return FieldRange{0.0f, std::numeric_limits<float>::max()};
        }
        //! 0 から 1 までの値
        [[nodiscard]] static constexpr FieldRange Unit() noexcept { return FieldRange{0.0f, 1.0f}; }

        [[nodiscard]] constexpr bool Contains(float value) const noexcept { return value >= min && value <= max; }
    };

    //! 登録の行に範囲を書かなかった事を表す型。この型を受けた書き込みは範囲を確かめない
    struct NoFieldRange
    {
    };

    //! 欄へ書いた結果。書けなかった時は欄の今の値が残る
    enum class [[nodiscard]] FieldWriteResult
    {
        Written,
        NonFinite,  // float の成分のどれかが非数か無限大
        OutOfRange, // 登録の行に書いた範囲の外
    };

    //! ログに書く、書けなかった理由の文
    [[nodiscard]] constexpr const char* FieldWriteResultText(FieldWriteResult result) noexcept
    {
        switch (result)
        {
        case FieldWriteResult::Written:
            return "書いた";
        case FieldWriteResult::NonFinite:
            return "非数か無限大";
        case FieldWriteResult::OutOfRange:
            return "範囲の外";
        }
        return "不明な結果";
    }

    // 登録のマクロの付け足しの引数から、見せる名前と範囲を型で拾う。両方を書く時は名前が先
    // 範囲を先に書くと合う重ね定義が無く、コンパイルエラーになる
    [[nodiscard]] constexpr const char* FieldLabelOf() noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr const char* FieldLabelOf(const char* label) noexcept
    {
        return label;
    }
    [[nodiscard]] constexpr const char* FieldLabelOf(FieldRange) noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr const char* FieldLabelOf(const char* label, FieldRange) noexcept
    {
        return label;
    }
    [[nodiscard]] constexpr NoFieldRange FieldRangeOf() noexcept
    {
        return NoFieldRange{};
    }
    [[nodiscard]] constexpr NoFieldRange FieldRangeOf(const char*) noexcept
    {
        return NoFieldRange{};
    }
    [[nodiscard]] constexpr FieldRange FieldRangeOf(FieldRange range) noexcept
    {
        return range;
    }
    [[nodiscard]] constexpr FieldRange FieldRangeOf(const char*, FieldRange range) noexcept
    {
        return range;
    }

    // 捨てる枝の型検査を避けるため、マクロの外のテンプレートで分岐する
    template <class Field> void ReadFieldValue(const Field& source, void* out) noexcept
    {
        if constexpr (IsAssetPointer<Field>::value)
        {
            static_cast<std::string*>(out)->assign(AssetFieldName(source));
        }
        else if constexpr (std::is_same_v<Field, EffectName> || std::is_same_v<Field, SoundName>)
        {
            *static_cast<std::string*>(out) = source.name;
        }
        else
        {
            *static_cast<Field*>(out) = source;
        }
    }

    template <class Field> void LoadFieldAsset(Field& field)
    {
        if constexpr (IsAssetPointer<Field>::value)
        {
            // 同じ道で引き直す。共有のマテリアルを登録した後や、読めなかった .mat を直した後は別の素材が返る
            LoadAssetField(field, AssetFieldName(field));
        }
    }

    //! 非有限か範囲の外なら書かずに理由を返す。ログへの出し方は呼ぶ側が決める
    template <class Field, class Range>
    FieldWriteResult AssignFieldValue(Field& target, const void* in, [[maybe_unused]] const Range& range) noexcept
    {
        if constexpr (std::is_same_v<Range, FieldRange>)
        {
            static_assert(std::is_same_v<Field, float>, "範囲を付けられるのは float の欄だけ");
        }

        if constexpr (IsAssetPointer<Field>::value)
        {
            LoadAssetField(target, *static_cast<const std::string*>(in));
            return FieldWriteResult::Written;
        }
        else if constexpr (std::is_same_v<Field, EffectName> || std::is_same_v<Field, SoundName>)
        {
            target.name = *static_cast<const std::string*>(in);
            return FieldWriteResult::Written;
        }
        else
        {
            const Field& value = *static_cast<const Field*>(in);
            if (!IsFiniteFieldValue(value))
            {
                return FieldWriteResult::NonFinite;
            }
            if constexpr (std::is_same_v<Range, FieldRange>)
            {
                if (!range.Contains(value))
                {
                    return FieldWriteResult::OutOfRange;
                }
            }
            target = value;
            return FieldWriteResult::Written;
        }
    }

    //! 項目の値をどこで変えてよいか。登録のマクロで決まる
    enum class FieldScope
    {
        Shared,    // NS_FIELD。元の .actor だけに書け、派生も含めてそのクラスの全部で共有する
        Protected, // NS_PROTECTED_FIELD。元と派生の .actor に書け、派生ごとに共有する
        Global,    // NS_GLOBAL_FIELD。元と派生の .actor とシーンに書ける
    };

    //! 型消去したフィールドの取得・設定関数。obj は宣言した具象型を指す
    struct FieldDesc
    {
        const char* name; // データの名前。保存と読み込みの鍵で、変えると保存済みの値が既定へ戻る
        FieldType type;
        void (*get)(const void* obj, void* outValue) noexcept; // obj から値を outValue へ取り出す
        // inValue を obj へ書き込む。非有限か範囲の外なら書かずに理由を返す。[[nodiscard]] は戻り値の型に付ける
        FieldWriteResult (*set)(void* obj, const void* inValue) noexcept;
        void (*load)(void* obj) = nullptr;                    // 素材の欄の素材を引き直す。ほかの型の欄では何もしない
        AssetKind assetKind = AssetKind::None;
        const char* label = nullptr; // インスペクタに見せる名前。変えても保存の鍵は変わらない
        // 参照の欄は、種類にかかわらずシーンが持つ
        FieldScope scope = FieldScope::Shared;
    };

    //! インスペクタに見せる名前。label を書かない欄は name を返す
    [[nodiscard]] inline const char* FieldLabel(const FieldDesc& field) noexcept
    {
        if (field.label != nullptr)
        {
            return field.label;
        }
        return field.name;
    }

    //! @brief 欄の見出し 1 つ。インスペクタが firstField の欄の前に畳める区切りを出す
    //! @details 見出しは次の見出しの手前の欄までを束ねる。次の見出しと firstField が同じなら欄を持たない
    struct FieldGroup
    {
        const char* name;
        std::size_t firstField; // この見出しの最初の欄の、fields の添字
    };

    // 保存キーと固定メンバーへのアクセス。所有や部品の交換は行わない
    struct ClassObjInfo
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
        const ClassObjInfo* classObjs = nullptr;
        std::size_t classObjCount = 0;
    };

    // マクロが宣言順に積む、値の欄・見出し・部品メンバーのいずれか
    struct ReflectEntry
    {
        FieldDesc field;   // 欄。見出しの項目では空
        const char* group; // 見出しの名前。欄の項目では nullptr
        ClassObjInfo classObj{};
    };

    //! 型ごとの不変なフィールド配列。ReflectionInfo が参照するためコピーしない
    class ReflectedFields
    {
    public:
        explicit ReflectedFields(std::span<const ReflectEntry> entries) noexcept
        {
            for (const ReflectEntry& entry : entries)
            {
                if (entry.classObj.key != nullptr)
                {
                    m_classObjs.push_back(entry.classObj);
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
                                  m_classObjs.data(),
                                  m_classObjs.size()};
        }

    private:
        std::vector<ClassObjInfo> m_classObjs;
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

//! 宣言済みのメンバを 1 フィールドとして登録する。メンバの宣言はしないので、クラスに別に書く
//! private も入れ子の struct のメンバ (m_tuning.speed) も書ける
//! 型タグと素材の種類はメンバ型から推論する。色は NS::Color、エフェクトは EffectName、音は SoundName の型で欄の種類が決まる
//! float と float の組と曲線の欄は IsFiniteFieldValue を通るので、非有限値の書き込みは捨てて元の値が残る (NS_ACCESSOR も同じ)
//! name は保存の鍵。3 つ目に見せる名前を書くと、インスペクタは name の代わりにそれを見せる
//! float の欄は、見せる名前の後に範囲 (NS::FieldRange) も書ける。範囲の外の書き込みは捨てて元の値が残る
//! 例: NS_FIELD(m_distance, "距離", NS::FieldRange::Positive()) / NS_FIELD(m_distance, "距離", "見せる名前", NS::FieldRange{0.0f, 1.0f})
#define NS_FIELD(member, name, ...) NS_FIELD_ENTRY(member, name, NS::FieldScope::Shared, __VA_ARGS__)

//! NS_FIELD に、派生の .actor でも値を変えてよい項目として登録する。引数は NS_FIELD と同じ
#define NS_PROTECTED_FIELD(member, name, ...) NS_FIELD_ENTRY(member, name, NS::FieldScope::Protected, __VA_ARGS__)

//! NS_FIELD に、シーンに置いたアクターでも値を変えてよい項目として登録する。引数は NS_FIELD と同じ
#define NS_GLOBAL_FIELD(member, name, ...) NS_FIELD_ENTRY(member, name, NS::FieldScope::Global, __VA_ARGS__)

// NS_FIELD・NS_PROTECTED_FIELD・NS_GLOBAL_FIELD の中身。直には書かない
#define NS_FIELD_ENTRY(member, name, fieldScope, ...)                                                                  \
    NS::ReflectEntry{                                                                                                  \
        NS::FieldDesc{                                                                                                 \
            name,                                                                                                      \
            NS::FieldTypeOf<decltype(Self::member)>(),                                                                 \
            +[](const void* c, void* out) noexcept { NS::ReadFieldValue(static_cast<const Self*>(c)->member, out); },  \
            +[](void* c, const void* in) noexcept {                                                                    \
                return NS::AssignFieldValue(static_cast<Self*>(c)->member, in, NS::FieldRangeOf(__VA_ARGS__));         \
            },                                                                                                         \
            +[](void* c) { NS::LoadFieldAsset(static_cast<Self*>(c)->member); },                                       \
            NS::AssetKindOf<decltype(Self::member)>(),                                                                 \
            NS::FieldLabelOf(__VA_ARGS__),                                                                             \
            fieldScope},                                                                                               \
        nullptr},

//! 欄の見出しを置く。次の見出しまでに並べた欄がこの見出しの下に入る。見出しは欄の配列に入らず、保存の鍵にならない
#define NS_GROUP(groupName) NS::ReflectEntry{NS::FieldDesc{}, groupName},

//! 基底の private や検証付きフィールドを getter/setter 経由で登録する。getter は値返し、setter は 1 引数
//! 見せる名前は NS_FIELD と同じく、最後の引数に書く。範囲 (NS::FieldRange) は書けない
#define NS_ACCESSOR(ValueType, name, getterCall, setterCall, ...)                                                      \
    NS_ACCESSOR_ENTRY(ValueType, name, getterCall, setterCall, NS::FieldScope::Shared, __VA_ARGS__)

//! NS_ACCESSOR に、シーンに置いたアクターでも値を変えてよい項目として登録する。引数は NS_ACCESSOR と同じ
#define NS_GLOBAL_ACCESSOR(ValueType, name, getterCall, setterCall, ...)                                               \
    NS_ACCESSOR_ENTRY(ValueType, name, getterCall, setterCall, NS::FieldScope::Global, __VA_ARGS__)

// NS_ACCESSOR・NS_GLOBAL_ACCESSOR の中身。直には書かない
#define NS_ACCESSOR_ENTRY(ValueType, name, getterCall, setterCall, fieldScope, ...)                                    \
    NS::ReflectEntry{NS::FieldDesc{name,                                                                               \
                                   NS::FieldTypeOf<ValueType>(),                                                       \
                                   +[](const void* c, void* out) noexcept {                                            \
                                       *static_cast<ValueType*>(out) = static_cast<const Self*>(c)->getterCall;        \
                                   },                                                                                  \
                                   +[](void* c, const void* in) noexcept {                                             \
                                       const ValueType& value = *static_cast<const ValueType*>(in);                    \
                                       if (!NS::IsFiniteFieldValue(value))                                             \
                                       {                                                                               \
                                           return NS::FieldWriteResult::NonFinite;                                     \
                                       }                                                                               \
                                       static_cast<Self*>(c)->setterCall(value);                                       \
                                       return NS::FieldWriteResult::Written;                                           \
                                   },                                                                                  \
                                   nullptr,                                                                            \
                                   NS::AssetKind::None,                                                                \
                                   {__VA_ARGS__},                                                                      \
                                   fieldScope},                                                                        \
                     nullptr},

//! フィールド宣言の終了。static なリフレクション情報を組み立てて返し、仮想の GetReflection はそこへ転送する
#define NS_REFLECT_END()                                                                                               \
    NS_REFLECT_END_VALUE()                                                                                             \
    [[nodiscard]] const NS::ReflectionInfo* GetReflection() const noexcept override                                    \
    {                                                                                                                  \
        return StaticReflection();                                                                                     \
    }

// 保存キーは既存データとの対応に使う。型が同じ部品もメンバーごとに区別する
#define NS_CLASSOBJ(memberName, savedKey)                                                                              \
    NS::ReflectEntry{NS::FieldDesc{},                                                                                  \
                     nullptr,                                                                                          \
                     NS::ClassObjInfo{savedKey,                                                                        \
                                      +[](const NS::Object* owner) noexcept -> NS::Object* {                           \
                                          return static_cast<const Self*>(owner)->memberName.get();                    \
                                      },                                                                               \
                                      +[](const NS::Object* owner) noexcept -> const void* {                           \
                                          return &static_cast<const Self*>(owner)->memberName;                         \
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
