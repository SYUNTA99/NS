#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace NS::Obj
{
    //! @brief used に無い名前を返す
    //! @details 空は Object とし、重複したら UE と同じく _1, _2 と番号を付ける
    //! 配置物の名前をシーンの中で一意にするのに使う
    [[nodiscard]] std::string MakeUniqueActorName(std::string_view base, const std::unordered_set<std::string>& used);
} // namespace NS::Obj
