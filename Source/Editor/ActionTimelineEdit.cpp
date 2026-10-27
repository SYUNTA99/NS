#include "Editor/ActionTimelineEdit.h"

#include "Editor/HitPreview.h"

#include <algorithm>

namespace NS::Editor
{
        std::optional<int> ActionOriginIndex(const HitPreviewResult& preview, std::string_view actionName) noexcept
        {
            for (std::size_t i = 0; i < preview.frames.size(); ++i)
            {
                const HitPreviewFrame& frame = preview.frames[i];
                if (frame.action == actionName && frame.actionFrame >= 0)
                {
                    return static_cast<int>(i) - frame.actionFrame;
                }
            }
            return std::nullopt;
        }

        std::size_t AddActionEvent(ActorAction& action, const HitReactionItem& item, int start)
        {
            ActorActionEvent event;
            event.item = item;
            event.start = std::max(start, 0);
            action.events.push_back(event);
            return action.events.size() - 1;
        }

        bool RemoveActionEvent(ActorAction& action, std::size_t row) noexcept
        {
            if (row >= action.events.size())
            {
                return false;
            }
            action.events.erase(action.events.begin() + static_cast<std::ptrdiff_t>(row));
            return true;
        }

        TimelineFrameRange ActionFrameRange(const ActorAction& action, const HitPreviewResult* preview) noexcept
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
            if (preview == nullptr || preview->frames.empty())
            {
                return range;
            }
            const std::optional<int> origin = ActionOriginIndex(*preview, action.name);
            if (!origin.has_value())
            {
                return range;
            }
            range.first = std::min(range.first, -*origin);
            range.last = std::max(range.last, static_cast<int>(preview->frames.size()) - 1 - *origin);
            return range;
        }

        std::vector<int> RowStartFrames(const HitPreviewResult& preview, std::string_view actionName, std::size_t row)
        {
            std::vector<int> starts;
            for (const HitPreviewFrame& frame : preview.frames)
            {
                if (frame.action != actionName)
                {
                    continue;
                }
                for (const std::size_t started : frame.startedEvents)
                {
                    if (started == row)
                    {
                        starts.push_back(frame.actionFrame);
                    }
                }
            }
            return starts;
        }
} // namespace NS::Editor
