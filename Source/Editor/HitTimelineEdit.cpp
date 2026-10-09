#include "Editor/HitTimelineEdit.h"

#include "Editor/HitPreview.h"

#include <algorithm>

namespace NS::Editor
{

    std::size_t AddHitEvent(GL::Level::HitTimeline& timeline,
                            const GL::Level::HitEventValue& value,
                            int start)
    {
        GL::Level::HitEvent event;
        event.value = value;
        event.start = start;
        if (start < 0 && !GL::Level::CanStartBeforeContact(value))
        {
            event.start = 0;
        }
        timeline.events.push_back(event);
        return timeline.events.size() - 1;
    }

    bool RemoveHitEvent(GL::Level::HitTimeline& timeline, std::size_t row) noexcept
    {
        if (row >= timeline.events.size())
        {
            return false;
        }
        timeline.events.erase(timeline.events.begin() + static_cast<std::ptrdiff_t>(row));
        return true;
    }

    TimelineFrameRange HitTimelineFrameRange(const GL::Level::HitTimeline& timeline,
                                             const HitPreviewResult* preview) noexcept
    {
        TimelineFrameRange range;
        for (const GL::Level::HitEvent& event : timeline.events)
        {
            range.first = std::min(range.first, event.start);
            // 長さ 1 の事象は始まりのフレームだけを占める
            range.last = std::max(range.last, event.start + std::max(event.length, 1) - 1);
        }
        if (preview != nullptr && preview->hit && !preview->frames.empty())
        {
            range.first = std::min(range.first, -preview->detectionIndex);
            range.last = std::max(range.last, static_cast<int>(preview->frames.size()) - 1 - preview->detectionIndex);
        }
        return range;
    }

    std::vector<int> RowStartFrames(const HitPreviewResult& preview, std::size_t row)
    {
        std::vector<int> starts;
        for (std::size_t i = 0; i < preview.frames.size(); ++i)
        {
            for (const std::size_t started : preview.frames[i].startedRows)
            {
                if (started == row)
                {
                    starts.push_back(static_cast<int>(i) - preview.detectionIndex);
                }
            }
        }
        return starts;
    }

} // namespace NS::Editor
