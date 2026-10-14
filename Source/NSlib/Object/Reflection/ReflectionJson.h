#pragma once

#include "NSlib/Object/SubObject.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)


namespace NS::Obj
{
    //! @brief 部品の欄を、配置物の JSON の "subObjects" の 1 件の形で書き出す
    //! @return {欄の名前: 値} に "enabled" を足した object
    [[nodiscard]] nlohmann::json SerializeSubObjFields(const SubObject& part);
    //! @brief comp のリフレクションの欄を {名前: 値} の object へ書き出す。"enabled" は含めない
    //! @details GetReflection() が nullptr の SubObject は空の object を返し、警告を出す
    [[nodiscard]] nlohmann::json SerializeSubObjectFields(const SubObject& comp);

    //! フィールド名を照合して値を適用する。欠損は現在値を保ち、型不一致は無視する
    //! 未知のキーを警告し、その件数を返す
    std::size_t ApplyJsonFields(SubObject& comp, const nlohmann::json& fields);

    //! owner を info の型として読み、フィールド名をキーに JSON へ書き出す
    [[nodiscard]] nlohmann::json SerializeReflectedFields(const void* owner, const ReflectionInfo& info);

    //! owner を info の型として扱い、JSON の値を適用する。処理と戻り値は ApplyJsonFields と同じ
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
