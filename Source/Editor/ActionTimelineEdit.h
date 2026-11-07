#pragma once

#include "Editor/Timeline.h"
#include "NSlib/CoreObject/Action/ActorActionInfo.h"

#include <cstddef>

namespace NS
{
    namespace Editor
    {
        //! 末尾に事象を足し、行番号を返す。start は状態に入った歩を 0 とするフレーム（負は 0 に補正、長さは既定の 1）
        std::size_t AddActionEvent(ActorActionInfo& action, const HitReactionItem& item, int start);

        //! row の行の事象を消し、後ろの行を詰める。row が範囲外なら false を返す
        bool RemoveActionEvent(ActorActionInfo& action, std::size_t row) noexcept;

        //! 事象と動作の長さを含む範囲（状態に入った歩が 0、該当なしは 0〜0）
        [[nodiscard]] TimelineFrameRange ActionFrameRange(const ActorActionInfo& action) noexcept;
    } // namespace Editor
} // namespace NS
