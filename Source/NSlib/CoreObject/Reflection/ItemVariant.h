#pragma once

#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

namespace NS
{
    // .actor の項目の並び (当たりの束の項目・動作の事象の項目) を std::variant で持つ型に共通の関数。
    // 種類の型は typeName (ファイルの種類の名前) と label (エディタに出す名前) を持ち、欄は NS_REFLECT で並べる

    //! Kind が NS_REFLECT で欄を並べている場合 true
    template <class Kind>
    concept HasReflectedItemFields = requires { Kind::StaticReflection(); };

    //! item の今の種類の、ファイルに書く種類の名前
    template <class Variant> [[nodiscard]] std::string_view ItemTypeName(const Variant& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> std::string_view { return value.typeName; }, item);
    }

    //! item の今の種類の、エディタに出す名前
    template <class Variant> [[nodiscard]] std::string_view ItemLabel(const Variant& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> std::string_view { return value.label; }, item);
    }

    namespace detail
    {
        template <class Variant, class Kind> void TryMakeItem(std::string_view name, std::optional<Variant>& result)
        {
            if (!result.has_value() && name == Kind{}.typeName)
            {
                result.emplace(std::in_place_type<Kind>);
            }
        }

        template <class Variant, class... Kinds>
        [[nodiscard]] std::optional<Variant> MakeItemByName(std::string_view name,
                                                            std::type_identity<std::variant<Kinds...>>)
        {
            std::optional<Variant> result;
            (TryMakeItem<Variant, Kinds>(name, result), ...);
            return result;
        }
    } // namespace detail

    //! 種類の名前から、欄が全部既定値のその種類の値を作る
    //! @return Variant の種類に無い名前の場合は std::nullopt
    template <class Variant> [[nodiscard]] std::optional<Variant> MakeItem(std::string_view typeName)
    {
        return detail::MakeItemByName<Variant>(typeName, std::type_identity<Variant>{});
    }

    //! item の今の種類のリフレクション。欄の無い種類は nullptr
    template <class Variant> [[nodiscard]] const ReflectionInfo* ItemReflection(const Variant& item) noexcept
    {
        return std::visit(
            []<class Kind>(const Kind&) -> const ReflectionInfo* {
                if constexpr (HasReflectedItemFields<Kind>)
                {
                    return Kind::StaticReflection();
                }
                else
                {
                    return nullptr;
                }
            },
            item);
    }

    //! item の今の種類の値を指す番地。ItemReflection の欄を読み書きする時に渡す
    template <class Variant> [[nodiscard]] void* ItemFields(Variant& item) noexcept
    {
        return std::visit([]<class Kind>(Kind& value) -> void* { return &value; }, item);
    }

    template <class Variant> [[nodiscard]] const void* ItemFields(const Variant& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> const void* { return &value; }, item);
    }
} // namespace NS
