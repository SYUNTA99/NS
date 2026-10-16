#pragma once

#include "NSlib/CoreObject/Object.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS
{
    //! @brief comp のリフレクションの欄を、配置物の JSON の "subObjects" の 1 件の形 {名前: 値} で書き出す
    //! @details GetReflection() が nullptr の部品は空の object を返し、警告を出す
    [[nodiscard]] nlohmann::json SerializeSubObjectFields(const Object& comp);

    //! フィールド名を照合して値を適用する。欠損は現在値を保ち、型不一致は無視する
    //! 未知のキーを警告し、その件数を返す
    std::size_t ApplyJsonFields(Object& comp, const nlohmann::json& fields);

    //! owner を info の型として読み、フィールド名をキーに JSON へ書き出す
    [[nodiscard]] nlohmann::json SerializeReflectedFields(const void* owner, const ReflectionInfo& info);

    //! owner を info の型として扱い、JSON の値を適用する。処理と戻り値は ApplyJsonFields と同じ
    std::size_t ApplyReflectedFields(void* owner, const ReflectionInfo& info, const nlohmann::json& fields);
} // namespace NS
