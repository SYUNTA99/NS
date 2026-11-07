#include "Editor/ActionTimelineEdit.h"

#include <algorithm>

namespace NS
{
    namespace Editor
    {
        std::size_t AddActionEvent(ActorActionInfo& action, const HitReactionItem& item, int start)
        {
            ActorActionEvent event;
            event.item = item;
            event.start = std::max(start, 0);
            action.events.push_back(event);
            return action.events.size() - 1;
        }

        bool RemoveActionEvent(ActorActionInfo& action, std::size_t row) noexcept
        {
            if (row >= action.events.size())
            {
                return false;
            }
            action.events.erase(action.events.begin() + static_cast<std::ptrdiff_t>(row));
            return true;
        }

        TimelineFrameRange ActionFrameRange(const ActorActionInfo& action) noexcept
        {
            TimelineFrameRange range;
            for (const ActorActionEvent& event : action.events)
            {
                range.first = std::min(range.first, event.start);
                // 長さ 1 の事象は始まりのフレームだけを占める
                range.last = std::max(range.last, event.start + std::max(event.length, 1) - 1);
            }
            // 長さを決めた動作は、事象が無くても状態の終わりまでを見せる
            if (action.length > 0)
            {
                range.last = std::max(range.last, action.length - 1);
            }
            return range;
        }
    } // namespace Editor
} // namespace NS
