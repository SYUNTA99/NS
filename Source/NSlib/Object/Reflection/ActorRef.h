#pragma once

#include <cstdint>

namespace NS::Obj
{
    //! 「object 無し」を表す永続 id の番兵。実 id は採番が 1 始まりで 0 を取らない
    //! ActorRef の欄の「0 は未設定」と同じ約束
    inline constexpr std::uint32_t k_NoObjectId = 0;

    //! 保存・並べ替えをまたいで配置物を指す永続 id。0 は未設定
    struct ActorRef
    {
        std::uint32_t id = 0; // 永続 id、0 は未設定

        //! 参照先が設定されているか。0 は未設定
        [[nodiscard]] constexpr bool IsSet() const noexcept { return id != 0; }

        [[nodiscard]] constexpr bool operator==(const ActorRef&) const noexcept = default;
    };
} // namespace NS::Obj
