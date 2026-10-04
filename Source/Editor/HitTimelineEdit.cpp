#include "Editor/HitTimelineEdit.h"

#include "Editor/HitPreview.h"

#include <algorithm>

namespace NS::Editor
{
    namespace
    {
        // 再生の速さ 1 で 1 秒に進むフレーム数。ゲームの固定の 1 歩と同じ
        constexpr float k_FramesPerSecond = 60.0f;
    } // namespace

    std::size_t AddHitEvent(NS::Game::Level::HitTimeline& timeline,
                            const NS::Game::Level::HitEventValue& value,
                            int start)
    {
        NS::Game::Level::HitEvent event;
        event.value = value;
        event.start = start;
        if (start < 0 && !NS::Game::Level::CanStartBeforeContact(value))
        {
            event.start = 0;
        }
        timeline.events.push_back(event);
        return timeline.events.size() - 1;
    }

    bool RemoveHitEvent(NS::Game::Level::HitTimeline& timeline, std::size_t row) noexcept
    {
        if (row >= timeline.events.size())
        {
            return false;
        }
        timeline.events.erase(timeline.events.begin() + static_cast<std::ptrdiff_t>(row));
        return true;
    }

    HitPreviewFrameRange HitTimelineFrameRange(const NS::Game::Level::HitTimeline& timeline,
                                               const HitPreviewResult* preview) noexcept
    {
        HitPreviewFrameRange range;
        for (const NS::Game::Level::HitEvent& event : timeline.events)
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

    void HitPreviewPlayback::Tick(float seconds, int lastFrame) noexcept
    {
        if (!playing)
        {
            return;
        }
        carry += seconds * k_FramesPerSecond * speed;
        // 浮動小数の足し込みで 1 にわずかに届かない分を拾う
        const int whole = static_cast<int>(carry + 1.0e-4f);
        carry -= static_cast<float>(whole);
        frame += whole;
        if (frame >= lastFrame)
        {
            frame = std::max(lastFrame, 0);
            playing = false;
            carry = 0.0f;
        }
    }

    void HitPreviewPlayback::StepBy(int steps, int lastFrame) noexcept
    {
        playing = false;
        carry = 0.0f;
        frame = std::clamp(frame + steps, 0, std::max(lastFrame, 0));
    }
} // namespace NS::Editor
