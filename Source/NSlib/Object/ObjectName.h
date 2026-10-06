#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace NS::Obj
{
    //! @brief used に無い名前を返す
    //! @details 空は Object とし、重複したら UE と同じく _1, _2 と番号を付ける
    //! 配置物はシーンの中、Component は持ち主の配置物の中で、それぞれ一意にするのに使う
    [[nodiscard]] std::string MakeUniqueObjectName(std::string_view base, const std::unordered_set<std::string>& used);
} // namespace NS::Obj
