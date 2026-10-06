#include "Editor/Timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    void TimelinePlayback::Tick(float seconds, TimelineFrameRange range) noexcept
    {
        if (!playing || range.last < range.first || !std::isfinite(seconds) || seconds < 0.0f ||
            !std::isfinite(speed) || speed <= 0.0f || !std::isfinite(framesPerSecond) || framesPerSecond <= 0.0f)
        {
            return;
        }
        frame = std::clamp(frame, range.first, range.last);
        const double advance = static_cast<double>(seconds) * framesPerSecond * speed + carry;
        const double remaining = static_cast<double>(range.last) - frame;
        if (advance + 1.0e-4 >= remaining)
        {
            frame = range.last;
            playing = false;
            carry = 0.0f;
            return;
        }
        const int whole = static_cast<int>(advance + 1.0e-4);
        frame += whole;
        carry = std::max(static_cast<float>(advance - whole), 0.0f);
    }

    void TimelinePlayback::Seek(int requestedFrame, TimelineFrameRange range) noexcept
    {
        playing = false;
        carry = 0.0f;
        frame = std::clamp(requestedFrame, range.first, std::max(range.first, range.last));
    }

    void TimelinePlayback::StepBy(int steps, TimelineFrameRange range) noexcept
    {
        const std::int64_t requested = static_cast<std::int64_t>(frame) + steps;
        Seek(static_cast<int>(std::clamp(requested,
                                         static_cast<std::int64_t>(range.first),
                                         static_cast<std::int64_t>(std::max(range.first, range.last)))),
             range);
    }

    void TimelinePlayback::Toggle(TimelineFrameRange range) noexcept
    {
        if (playing)
        {
            playing = false;
            return;
        }
        if (range.last <= range.first)
        {
            Seek(range.first, range);
            return;
        }
        if (frame >= range.last || frame < range.first)
        {
            Seek(range.first, range);
        }
        playing = true;
    }

    bool DrawTimelinePlayback(TimelinePlayback& playback, TimelineFrameRange range, bool enabled) noexcept
    {
#if NS_EDITOR_ENABLED
        bool changed = false;
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button("|<"))
        {
            playback.Seek(range.first, range);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("<"))
        {
            playback.StepBy(-1, range);
            changed = true;
        }
        ImGui::SameLine();
        const char* label = "再生";
        if (playback.playing)
        {
            label = "止める";
        }
        if (ImGui::Button(label))
        {
            playback.Toggle(range);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(">"))
        {
            playback.StepBy(1, range);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        (void)ImGui::DragFloat("速さ", &playback.speed, 0.05f, 0.05f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::Text("フレーム %+d", playback.frame);
        return changed;
#else
        (void)playback;
        (void)range;
        (void)enabled;
        return false;
#endif
    }

    bool DrawTimelineTracks(std::span<const TimelineTrack> tracks,
                            TimelineFrameRange range,
                            TimelinePlayback& playback,
                            std::optional<std::size_t>& selected,
                            bool seekEnabled,
                            float extraHeight,
                            const std::function<void(const TimelineLayout&)>& extraRows,
                            std::optional<TimelineFrameRange> seekRange) noexcept
    {
#if NS_EDITOR_ENABLED
        range.last = std::max(range.first, range.last);
        const float count = static_cast<float>(static_cast<double>(range.last) - range.first + 1.0);
        const float rowHeight = ImGui::GetFrameHeight();
        const float labelWidth = 170.0f;
        const float frameWidth = std::max((ImGui::GetContentRegionAvail().x - labelWidth) / count, 4.0f);
        const float totalWidth = frameWidth * count;
        const float rowsHeight = (rowHeight + ImGui::GetStyle().ItemSpacing.y) * static_cast<float>(tracks.size() + 1);
        const float height = std::min(rowsHeight, 360.0f) + extraHeight + ImGui::GetStyle().ScrollbarSize +
                             ImGui::GetStyle().ItemSpacing.y * 2.0f;
        if (!ImGui::BeginChild(
                "##timeline", ImVec2{0.0f, height}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::EndChild();
            return false;
        }
        bool moved = false;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const float left = top.x + labelWidth;
        ImGui::Dummy(ImVec2{labelWidth, rowHeight});
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##ruler", ImVec2{totalWidth, rowHeight});
        if (ImGui::IsItemActive() && seekEnabled)
        {
            const float offset = std::clamp((ImGui::GetIO().MousePos.x - left) / frameWidth, 0.0f, count - 1.0f);
            playback.Seek(range.first + static_cast<int>(offset), seekRange.value_or(range));
            moved = true;
        }
        for (int i = 0; i < static_cast<int>(count); ++i)
        {
            const int clock = range.first + i;
            if (clock % 5 != 0)
            {
                continue;
            }
            const float x = left + static_cast<float>(i) * frameWidth;
            char text[32];
            std::snprintf(text, sizeof(text), "%d", clock);
            draw->AddLine(
                ImVec2{x, top.y + rowHeight * 0.6f}, ImVec2{x, top.y + rowHeight}, IM_COL32(200, 200, 200, 255));
            draw->AddText(ImVec2{x + 2.0f, top.y}, IM_COL32(200, 200, 200, 255), text);
        }
        for (std::size_t row = 0; row < tracks.size(); ++row)
        {
            const TimelineTrack& track = tracks[row];
            ImGui::PushID(static_cast<int>(row));
            const bool isSelected = selected.has_value() && *selected == row;
            if (ImGui::Selectable(track.label.c_str(), isSelected, 0, ImVec2{labelWidth - 4.0f, rowHeight}))
            {
                selected = row;
            }
            ImGui::SameLine(labelWidth, 0.0f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##band", ImVec2{totalWidth, rowHeight}))
            {
                selected = row;
            }
            const float x0 = origin.x + static_cast<float>(track.start - range.first) * frameWidth;
            const float x1 = x0 + static_cast<float>(std::max(track.length, 1)) * frameWidth;
            ImU32 color = IM_COL32(90, 140, 210, 255);
            if (isSelected)
            {
                color = IM_COL32(240, 170, 60, 255);
            }
            draw->AddRectFilled(ImVec2{x0, origin.y + 2.0f}, ImVec2{x1, origin.y + rowHeight * 0.65f}, color, 2.0f);
            for (const int clock : track.startedFrames)
            {
                const float x = origin.x + static_cast<float>(clock - range.first) * frameWidth;
                draw->AddRectFilled(ImVec2{x, origin.y + rowHeight * 0.72f},
                                    ImVec2{x + std::max(frameWidth, 3.0f), origin.y + rowHeight - 1.0f},
                                    IM_COL32(250, 230, 90, 255));
            }
            ImGui::PopID();
        }
        if (extraRows)
        {
            extraRows(TimelineLayout{range.first, labelWidth, frameWidth, totalWidth, rowHeight});
        }
        const float bottom = ImGui::GetCursorScreenPos().y;
        if (range.first <= 0 && range.last >= 0)
        {
            const float x = left + static_cast<float>(-range.first) * frameWidth;
            draw->AddLine(ImVec2{x, top.y}, ImVec2{x, bottom}, IM_COL32(200, 200, 200, 120), 1.0f);
        }
        if (seekEnabled)
        {
            const float x = left + (static_cast<float>(playback.frame - range.first) + 0.5f) * frameWidth;
            draw->AddLine(ImVec2{x, top.y}, ImVec2{x, bottom}, IM_COL32(255, 80, 80, 255), 2.0f);
        }
        ImGui::EndChild();
        return moved;
#else
        (void)tracks;
        (void)range;
        (void)playback;
        (void)selected;
        (void)seekEnabled;
        (void)extraHeight;
        (void)extraRows;
        (void)seekRange;
        return false;
#endif
    }
} // namespace NS::Editor
