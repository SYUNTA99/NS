#pragma once

/// @file ObjectRef.h
/// @brief レベル配置物を保存をまたいで指す参照値型

#include <cstdint>

namespace NS::Scene
{
    /// 別の配置物への参照。 id はレベル側で採番される永続 id、 0 は未設定
    /// @details 実体ポインタや配列 index でなく永続 id で持つことで、 並び替え・保存・再読込・undo を
    /// またいでも同じ相手を指し続ける。 参照先の実体解決はレベル側の照合に委ねる
    struct ObjectRef
    {
        std::uint32_t id = 0;

        /// 参照先が設定されているか。 0 は未設定
        [[nodiscard]] constexpr bool IsSet() const noexcept { return id != 0; }

        [[nodiscard]] constexpr bool operator==(const ObjectRef&) const noexcept = default;
    };
} // namespace NS::Scene
