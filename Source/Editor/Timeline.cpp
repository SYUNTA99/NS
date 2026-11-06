#include "Editor/Timeline.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

namespace NS
{
    namespace Editor
    {
        void TimelinePlayback::Seek(int requestedFrame, TimelineFrameRange range) noexcept
        {
            frame = std::clamp(requestedFrame, range.first, std::max(range.first, range.last));
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
            range.last = std::max(range.first, range.last);
            const float count = static_cast<float>(static_cast<double>(range.last) - range.first + 1.0);
            const float rowHeight = ImGui::GetFrameHeight();
            const float labelWidth = 170.0f;
            const float frameWidth = std::max((ImGui::GetContentRegionAvail().x - labelWidth) / count, 4.0f);
            const float totalWidth = frameWidth * count;
            const float rowsHeight =
                (rowHeight + ImGui::GetStyle().ItemSpacing.y) * static_cast<float>(tracks.size() + 1);
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
        }
    } // namespace Editor
} // namespace NS
