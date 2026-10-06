#pragma once

#include "NSlib/Object/Component.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

// リフレクションを辿って Component を {type, fields} JSON へ相互変換する
// 値は nlohmann::json を直接受け渡し、呼出側は object 配列へそのまま積める

namespace NS::Obj
{
    //! @brief 部品の欄を、配置物の JSON の "parts" の 1 件の形で書き出す
    //! @return {欄の名前: 値} に "enabled" を足した object
    [[nodiscard]] nlohmann::json SerializePartFields(const Component& part);
    //! @brief comp のリフレクションの欄を {名前: 値} の object へ書き出す。"enabled" は含めない
    //! @details GetReflection() が nullptr の component は空の object を返し、警告を出す
    [[nodiscard]] nlohmann::json SerializeComponentFields(const Component& comp);

    //! @brief fields object の各キーをリフレクション FieldDesc に照合し、一致する field を set で書き戻す
    //! @details 欠損キーは前方互換のため既定値のまま、型不一致は無視する
    //! 照合先の無いキーは値がどこにも入らないので 1 件ずつ警告を出す
    //! @return 照合先の無かったキーの数
    std::size_t ApplyJsonFields(Component& comp, const nlohmann::json& fields);

    //! @brief info の欄を持つ値を、{欄の名前: 値} の JSON object へ書き出す
    //! @details Component を継承しない値型 (NS_REFLECT_END_VALUE で登録した型) が使う
    //! @param[in] owner info の型の値を指す番地
    //! @param[in] info owner の型のリフレクション
    //! @return {欄の名前: 値} の object
    [[nodiscard]] nlohmann::json SerializeReflectedFields(const void* owner, const ReflectionInfo& info);

    //! @brief fields object の各キーを info の欄に照合し、一致する欄を owner へ書き戻す
    //! @details 欠損キー・型不一致・照合先の無いキーの扱いは ApplyJsonFields と同じ
    //! @param[in,out] owner info の型の値を指す番地
    //! @param[in] info owner の型のリフレクション
    //! @param[in] fields {欄の名前: 値} の object。object でなければ何も書かない
    //! @return 照合先の無かったキーの数
    std::size_t ApplyReflectedFields(void* owner, const ReflectionInfo& info, const nlohmann::json& fields);

    //! value の型 T のリフレクションで SerializeReflectedFields を呼ぶ
    template <class T> [[nodiscard]] nlohmann::json SerializeValueFields(const T& value)
    {
        return SerializeReflectedFields(&value, *T::StaticReflection());
    }

    //! value の型 T のリフレクションで ApplyReflectedFields を呼ぶ
    template <class T> std::size_t ApplyValueFields(T& value, const nlohmann::json& fields)
    {
        return ApplyReflectedFields(&value, *T::StaticReflection(), fields);
    }
} // namespace NS::Obj
