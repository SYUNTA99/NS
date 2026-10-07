#include "Editor/HitTimelinePanel.h"

#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/App/Application.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/HitSensor.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    namespace
    {
        using NS::Game::Level::HitDirection;
        using NS::Game::Level::HitEvent;
        using NS::Game::Level::HitEventValue;
        using NS::Game::Level::HitTier;

        // 事象の種類の並び。HitEventValue の選択肢の順で、種類を足すとここにも出る
        const std::vector<HitEventValue>& EventTypes()
        {
            static const std::vector<HitEventValue> s_types = []<std::size_t... I>(std::index_sequence<I...>) {
                return std::vector<HitEventValue>{HitEventValue{std::in_place_index<I>}...};
            }(std::make_index_sequence<std::variant_size_v<HitEventValue>>{});
            return s_types;
        }

        constexpr HitDirection k_Directions[] = {
            HitDirection::Any, HitDirection::Right, HitDirection::Left, HitDirection::Up, HitDirection::Down};

        // 向きのパネルに出す名前
        const char* DirectionLabel(HitDirection direction) noexcept
        {
            switch (direction)
            {
            case HitDirection::Any:
                return "どの向きでも";
            case HitDirection::Right:
                return "右の外れ";
            case HitDirection::Left:
                return "左の外れ";
            case HitDirection::Up:
                return "上の外れ";
            case HitDirection::Down:
                return "下の外れ";
            }
            return "?";
        }

        // 段のパネルに出す名前
        const char* TierLabel(HitTier tier) noexcept
        {
            if (tier == HitTier::Center)
            {
                return "真ん中";
            }
            return "外れ";
        }

        // 再生の速さ。12 フレームの止めはそのままの速さだと 0.2 秒で目で追えないので、遅い方を既定にする
        constexpr float k_QuarterSpeed = 0.25f;

#if NS_EDITOR_ENABLED
        constexpr int k_PreviewGraphCount = 5; // 帯の下の折れ線の行の数
        constexpr ImU32 k_GraphBackColor = IM_COL32(40, 40, 46, 255);
        constexpr ImU32 k_GraphTraumaColor = IM_COL32(255, 170, 60, 255);
        constexpr ImU32 k_GraphXColor = IM_COL32(240, 90, 90, 255);
        constexpr ImU32 k_GraphYColor = IM_COL32(110, 220, 110, 255);
        constexpr ImU32 k_GraphZColor = IM_COL32(110, 150, 255, 255);
        constexpr ImU32 k_GraphSpeedColor = IM_COL32(200, 120, 255, 255);
#endif
    } // namespace

    HitTimelinePanel::HitTimelinePanel()
    {
        m_playback.speed = k_QuarterSpeed;
        LoadWorking(m_tier);
        // 開いた最初のフレームから、自機に一番近い相手への真ん中の当たりを見せる
        m_needsRun = true;
    }

    void HitTimelinePanel::LoadWorking(HitTier tier)
    {
        m_tier = tier;
        const NS::Game::Level::HitTimeline* found =
            NS::Game::Level::HitTimelineLibrary::Get().Find(NS::Game::Level::HitTimelineNameOf(tier));
        if (found != nullptr)
        {
            m_working = *found;
        }
        else
        {
            m_working = NS::Game::Level::HitTimeline{};
        }
        m_selectedRow.reset();
    }

    void HitTimelinePanel::ApplyWorking()
    {
        NS::Game::Level::HitTimelineLibrary::Get().Set(NS::Game::Level::HitTimelineNameOf(m_tier), m_working);
        m_dirty = true;
        m_needsRun = true;
    }

    TimelineFrameRange HitTimelinePanel::PlaybackRange() const noexcept
    {
        if (!m_result.hit || m_result.frames.empty())
        {
            return TimelineFrameRange{0, -1};
        }
        return TimelineFrameRange{-m_result.detectionIndex,
                                  static_cast<int>(m_result.frames.size()) - 1 - m_result.detectionIndex};
    }

    void HitTimelinePanel::RunPreview(LevelEditorController& editor)
    {
        m_needsRun = false;
        m_snapshot = editor.SceneSnapshot();
        HitPreviewWorld world;
        if (NS::Application* app = NS::Application::Get())
        {
            world.assets = &app->Assets();
            world.renderer = &app->Renderer();
        }
        const bool wasHit = m_result.hit;
        HitPreviewDesc desc = m_desc;
        // 手入力の開始時刻が過大でも、取り直しを無制限に回さないための上限
        constexpr int k_MaxPreviewFrames = 10000;
        for (const HitTier tier : NS::Game::Level::HitTiers())
        {
            if (const NS::Game::Level::HitTimeline* timeline =
                    NS::Game::Level::HitTimelineLibrary::Get().FindForTier(tier))
            {
                for (const HitEvent& event : timeline->events)
                {
                    const std::int64_t end = static_cast<std::int64_t>(event.start) + std::max(event.length, 1) +
                                             desc.leadFrames + desc.framesAfterRebound;
                    desc.maxFrames =
                        std::max(desc.maxFrames,
                                 static_cast<int>(std::clamp(end, std::int64_t{0}, std::int64_t{k_MaxPreviewFrames})));
                }
            }
        }
        m_result = RunHitPreview(m_snapshot, desc, world);
        m_desc.targetId = m_result.desc.targetId;
        m_playback.playing = false;
        m_playback.carry = 0.0f;
        if (!m_result.hit || m_result.frames.empty())
        {
            m_preview.Clear();
            return;
        }
        const TimelineFrameRange range = PlaybackRange();
        if (!wasHit)
        {
            m_playback.Seek(0, range);
        }
        else
        {
            m_playback.Seek(m_playback.frame, range);
        }
        HitPreviewResult placement = m_result;
        placement.frames.clear();
        m_preview.Reset(
            [snapshot = m_snapshot, placement, world] { return MakeHitPreviewScene(snapshot, placement, world); },
            range);
    }

    void HitTimelinePanel::ResetPreview() noexcept
    {
        m_preview.Clear();
        m_playback.playing = false;
        m_playback.carry = 0.0f;
        m_needsRun = true;
    }

    TimelinePreview* HitTimelinePanel::Preview(LevelEditorController& editor)
    {
        if (editor.CurrentMode() != LevelEditorController::Mode::Edit || !m_showPreview || !m_result.hit)
        {
            return nullptr;
        }
        if (m_preview.Seek(m_playback.frame) == nullptr)
        {
            return nullptr;
        }
        return &m_preview;
    }

    void HitTimelinePanel::Tick(LevelEditorController& editor, float seconds)
    {
#if NS_EDITOR_ENABLED
        const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
        if (playMode)
        {
            ResetPreview();
        }
        else if (!ImGui::IsAnyItemActive() && (m_panelVisible || m_showPreview))
        {
            // 見えない間は場面を写して比べない。見える側へ戻れば、閉じていた間の変更を拾う
            if (m_snapshot != editor.SceneSnapshot())
            {
                m_needsRun = true;
            }
            if (m_needsRun)
            {
                RunPreview(editor);
            }
        }
        if (!playMode && m_result.hit && !m_needsRun)
        {
            m_playback.Tick(seconds, PlaybackRange());
        }
#else
        (void)editor;
        (void)seconds;
#endif
    }

    void HitTimelinePanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
        m_panelVisible = ImGui::Begin(k_PanelHitTimeline);
        if (m_panelVisible)
        {
            if (playMode)
            {
                ImGui::TextUnformatted("プレイ中は下見しない。編集へ戻ると下見できる");
            }
            else
            {
                RenderConditions(editor);
                ImGui::Separator();
                RenderFileButtons();
                RenderPlayback();
                RenderBands();
                RenderSelectedRow();
            }
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }

    void HitTimelinePanel::RenderConditions(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        ImGui::SeparatorText("当てる条件");
        bool changed = false;

        NS::Obj::Actor* selected = editor.SelectedObjectActor();
        const bool selectable =
            selected != nullptr && !editor.SelectedIsPlayerObject() && selected->BodySensorPart() != nullptr;
        if (m_desc.targetId == 0)
        {
            ImGui::TextUnformatted("相手: 自機に一番近い配置物");
        }
        else
        {
            ImGui::Text("相手: id %u", m_desc.targetId);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!selectable);
        if (ImGui::SmallButton("選択中の配置物を相手にする"))
        {
            m_desc.targetId = selected->Id();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("一番近い配置物にする"))
        {
            m_desc.targetId = 0;
            changed = true;
        }

        changed |= ImGui::SliderFloat("面の左右 (右が正)", &m_desc.faceU, -1.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("面の上下 (上が正)", &m_desc.faceV, -1.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("溜め (0 は通常突進)", &m_desc.charge01, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderInt("検知までのフレーム", &m_desc.leadFrames, 1, 30);

        struct Preset
        {
            const char* label;
            float u;
            float v;
        };
        constexpr Preset k_Presets[] = {
            {"真ん中", 0.0f, 0.0f},
            {"右の外れ", 0.9f, 0.0f},
            {"左の外れ", -0.9f, 0.0f},
            {"上の外れ", 0.0f, 0.9f},
            {"下の外れ", 0.0f, -0.9f},
        };
        for (std::size_t i = 0; i < std::size(k_Presets); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(k_Presets[i].label))
            {
                m_desc.faceU = k_Presets[i].u;
                m_desc.faceV = k_Presets[i].v;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("下見し直す"))
        {
            changed = true;
        }
        if (changed)
        {
            m_needsRun = true;
        }

        if (m_result.hit)
        {
            const NS::Game::Level::ImpactRecord& impact = m_result.impact;
            ImGui::Text("当たった段: %s / 面の位置 (%.2f, %.2f) / 突進を出して %d フレーム目に検知 / 威力 %.2f",
                        TierLabel(impact.tier),
                        impact.faceU,
                        impact.faceV,
                        m_result.detectionIndex,
                        impact.power);
            if (impact.tier != m_tier)
            {
                ImGui::TextColored(k_OverriddenFieldColor,
                                   "編集している段 (%s) と当たった段が違うので、帯に実際の始まりを重ねない",
                                   TierLabel(m_tier));
            }
        }
        else if (!m_result.error.empty())
        {
            ImGui::TextColored(k_OverriddenFieldColor, "下見できない: %s", m_result.error.c_str());
        }
#else
        (void)editor;
#endif
    }

    void HitTimelinePanel::RenderFileButtons() noexcept
    {
#if NS_EDITOR_ENABLED
        ImGui::SeparatorText("タイムライン");
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("編集する段", TierLabel(m_tier)))
        {
            for (const HitTier tier : NS::Game::Level::HitTiers())
            {
                if (ImGui::Selectable(TierLabel(tier), tier == m_tier))
                {
                    LoadWorking(tier);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存"))
        {
            NS::Game::Level::HitTimelineLibrary& library = NS::Game::Level::HitTimelineLibrary::Get();
            const std::string_view name = NS::Game::Level::HitTimelineNameOf(m_tier);
            library.Set(name, m_working);
            if (library.Save(name))
            {
                m_dirty = false;
                m_status = std::format("{}/{}.json へ書いた", library.Directory(), name);
            }
            else
            {
                m_status = std::format("{}.json を書けなかった", name);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("元に戻す"))
        {
            // 置き場ごと読み直すので、もう片方の段の保存していない変更も戻る
            NS::Game::Level::HitTimelineLibrary::Get().Reload();
            LoadWorking(m_tier);
            m_dirty = false;
            m_needsRun = true;
            m_status = "最後に保存した中身へ戻した (両方の段)";
        }
        if (m_dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(k_OverriddenFieldColor, "保存していない変更がある");
        }

        const std::vector<HitEventValue>& types = EventTypes();
        m_addType = std::clamp(m_addType, 0, static_cast<int>(types.size()) - 1);
        ImGui::SetNextItemWidth(160.0f);
        const std::string currentLabel{NS::Game::Level::HitEventLabel(types[static_cast<std::size_t>(m_addType)])};
        if (ImGui::BeginCombo("##add-type", currentLabel.c_str()))
        {
            for (std::size_t i = 0; i < types.size(); ++i)
            {
                const std::string label{NS::Game::Level::HitEventLabel(types[i])};
                if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == m_addType))
                {
                    m_addType = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("再生の位置に足す"))
        {
            int clock = 0;
            if (m_result.hit)
            {
                clock = m_playback.frame;
            }
            m_selectedRow = AddHitEvent(m_working, types[static_cast<std::size_t>(m_addType)], clock);
            ApplyWorking();
        }
        if (!m_status.empty())
        {
            ImGui::TextUnformatted(m_status.c_str());
        }
#endif
    }

    void HitTimelinePanel::RenderPlayback() noexcept
    {
#if NS_EDITOR_ENABLED
        if (ImGui::Checkbox("Game に下見を表示", &m_showPreview) && m_showPreview)
        {
            ImGui::SetWindowFocus(k_PanelGame);
        }
        const bool ready = m_result.hit && !m_needsRun;
        if (DrawTimelinePlayback(m_playback, PlaybackRange(), ready))
        {
            m_showPreview = true;
            ImGui::SetWindowFocus(k_PanelGame);
        }
        ImGui::TextUnformatted("検知を 0 としたフレーム。Game ビューで動きを確認");
#endif
    }

    void HitTimelinePanel::RenderBands() noexcept
    {
#if NS_EDITOR_ENABLED
        const HitPreviewResult* preview = nullptr;
        if (m_result.hit)
        {
            preview = &m_result;
        }
        const TimelineFrameRange range = HitTimelineFrameRange(m_working, preview);
        const bool overlay = m_result.hit && m_result.impact.tier == m_tier;
        std::vector<TimelineTrack> tracks;
        tracks.reserve(m_working.events.size());
        for (std::size_t row = 0; row < m_working.events.size(); ++row)
        {
            const HitEvent& event = m_working.events[row];
            TimelineTrack track;
            track.label = NS::Game::Level::HitEventLabel(event.value);
            if (event.direction != HitDirection::Any)
            {
                track.label += std::format(" ({})", DirectionLabel(event.direction));
            }
            track.start = event.start;
            track.length = event.length;
            if (overlay)
            {
                track.startedFrames = RowStartFrames(m_result, row);
            }
            tracks.push_back(std::move(track));
        }
        float graphsHeight = 0.0f;
        if (m_result.hit && !m_result.frames.empty())
        {
            graphsHeight = static_cast<float>(k_PreviewGraphCount) *
                           (ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y);
        }
        const bool moved = DrawTimelineTracks(
            tracks,
            range,
            m_playback,
            m_selectedRow,
            m_result.hit && !m_needsRun,
            graphsHeight,
            [this](const TimelineLayout& layout) {
                if (m_result.hit && !m_result.frames.empty())
                {
                    RenderPreviewGraphs(*ImGui::GetWindowDrawList(), layout);
                }
            },
            PlaybackRange());
        if (moved)
        {
            m_showPreview = true;
            ImGui::SetWindowFocus(k_PanelGame);
        }
#endif
    }

    void HitTimelinePanel::RenderPreviewGraphs(ImDrawList& draw, const TimelineLayout& layout) noexcept
    {
#if NS_EDITOR_ENABLED
        const int firstClock = layout.firstFrame;
        const float frameWidth = layout.frameWidth;
        const float totalWidth = layout.totalWidth;
        const float rowHeight = layout.rowHeight;
        const std::vector<HitPreviewFrame>& frames = m_result.frames;
        // 揺れの角度とずれは一番大きい所で縦を合わせる。0 しか無ければ 1 で割る
        float largestAngle = 0.0f;
        float largestOffset = 0.0f;
        float largestSink = 0.0f;
        for (const HitPreviewFrame& frame : frames)
        {
            largestAngle = std::max({largestAngle,
                                     std::fabs(frame.shakeAngles.x),
                                     std::fabs(frame.shakeAngles.y),
                                     std::fabs(frame.shakeAngles.z)});
            largestOffset = std::max({largestOffset, std::fabs(frame.shakeOffset.x), std::fabs(frame.shakeOffset.y)});
            largestSink = std::max(largestSink, std::fabs(frame.sinkPixels));
        }
        if (largestSink <= 0.0f)
        {
            largestSink = 1.0f;
        }
        if (largestAngle <= 0.0f)
        {
            largestAngle = 1.0f;
        }
        if (largestOffset <= 0.0f)
        {
            largestOffset = 1.0f;
        }
        const float graphHeight = rowHeight * 2.0f;
        const int detection = m_result.detectionIndex;
        // 1 本の折れ線。value は 0〜1 (center が真なら -1〜1) に写した値を返す
        const auto drawLine = [&](const ImVec2& origin,
                                  ImU32 color,
                                  bool centered,
                                  const std::function<float(const HitPreviewFrame&)>& value) {
            ImVec2 previous{};
            for (std::size_t i = 0; i < frames.size(); ++i)
            {
                const int clock = static_cast<int>(i) - detection;
                const float x = origin.x + (static_cast<float>(clock - firstClock) + 0.5f) * frameWidth;
                float ratio = value(frames[i]);
                if (centered)
                {
                    ratio = ratio * 0.5f + 0.5f;
                }
                const float y = origin.y + graphHeight - 2.0f - std::clamp(ratio, 0.0f, 1.0f) * (graphHeight - 4.0f);
                const ImVec2 point{x, y};
                if (i > 0)
                {
                    draw.AddLine(previous, point, color, 1.5f);
                }
                previous = point;
            }
        };
        const auto graphRow =
            [&](const char* label, const char* tooltip, const std::function<void(const ImVec2&)>& body) {
                ImGui::TextUnformatted(label);
                ImGui::SetItemTooltip("%s", tooltip);
                ImGui::SameLine(layout.labelWidth, 0.0f);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2{totalWidth, graphHeight});
                draw.AddRectFilled(origin, ImVec2{origin.x + totalWidth, origin.y + graphHeight}, k_GraphBackColor);
                body(origin);
            };
        graphRow("トラウマ", "カメラのトラウマ。下が 0、上が 1", [&](const ImVec2& origin) {
            drawLine(origin, k_GraphTraumaColor, false, [](const HitPreviewFrame& frame) { return frame.trauma; });
        });
        graphRow(
            "揺れの角度",
            "トラウマの揺れの角度。赤が横の首振り、緑が縦の首振り、青が傾き。真ん中が 0 で、一番大きい所で縦を合わせる",
            [&](const ImVec2& origin) {
                drawLine(origin, k_GraphXColor, true, [largestAngle](const HitPreviewFrame& frame) {
                    return frame.shakeAngles.x / largestAngle;
                });
                drawLine(origin, k_GraphYColor, true, [largestAngle](const HitPreviewFrame& frame) {
                    return frame.shakeAngles.y / largestAngle;
                });
                drawLine(origin, k_GraphZColor, true, [largestAngle](const HitPreviewFrame& frame) {
                    return frame.shakeAngles.z / largestAngle;
                });
            });
        graphRow("揺れのずれ",
                 "平行移動の揺れのずれ。赤が右、緑が上。真ん中が 0 で、一番大きい所で縦を合わせる",
                 [&](const ImVec2& origin) {
                     drawLine(origin, k_GraphXColor, true, [largestOffset](const HitPreviewFrame& frame) {
                         return frame.shakeOffset.x / largestOffset;
                     });
                     drawLine(origin, k_GraphYColor, true, [largestOffset](const HitPreviewFrame& frame) {
                         return frame.shakeOffset.y / largestOffset;
                     });
                 });
        graphRow("沈む揺れ",
                 "沈む揺れの画面の縦のずれ。真ん中が 0 で下が沈み、一番大きい所で縦を合わせる",
                 [&](const ImVec2& origin) {
                     drawLine(origin, k_GraphYColor, true, [largestSink](const HitPreviewFrame& frame) {
                         return frame.sinkPixels / largestSink;
                     });
                 });
        graphRow("世界の速さ", "世界の速さ。下が 0、上が普段の速さ 1", [&](const ImVec2& origin) {
            drawLine(origin, k_GraphSpeedColor, false, [](const HitPreviewFrame& frame) { return frame.worldSpeed; });
        });
#else
        (void)draw;
        (void)layout;
#endif
    }

    void HitTimelinePanel::RenderSelectedRow() noexcept
    {
#if NS_EDITOR_ENABLED
        if (!m_selectedRow.has_value() || *m_selectedRow >= m_working.events.size())
        {
            return;
        }
        HitEvent& event = m_working.events[*m_selectedRow];
        const std::string title{NS::Game::Level::HitEventLabel(event.value)};
        ImGui::SeparatorText(title.c_str());
        bool changed = false;
        if (BeginFieldTable("##event-row"))
        {
            FieldRow("始まり");
            int start = event.start;
            if (ImGui::DragInt("##start", &start, 0.2f))
            {
                // 触れる前に置けない種類はマイナスへ動かさない。読み込みが弾く形をパネルで作らない
                if (start < 0 && !NS::Game::Level::CanStartBeforeContact(event.value))
                {
                    start = 0;
                }
                event.start = start;
                changed = true;
            }
            FieldRow("長さ");
            int length = event.length;
            if (ImGui::DragInt("##length", &length, 0.2f, 0, 600))
            {
                event.length = std::max(length, 0);
                changed = true;
            }
            FieldRow("向き");
            if (ImGui::BeginCombo("##direction", DirectionLabel(event.direction)))
            {
                for (const HitDirection direction : k_Directions)
                {
                    if (ImGui::Selectable(DirectionLabel(direction), direction == event.direction))
                    {
                        event.direction = direction;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            EndFieldTable();
        }
        if (const NS::Obj::ReflectionInfo* info = NS::Game::Level::HitEventReflection(event.value))
        {
            const ValueEditResult fields = DrawReflectedValue(NS::Game::Level::HitEventFields(event.value), *info);
            changed |= fields.changed;
        }
        if (ImGui::Button("この事象を消す"))
        {
            (void)RemoveHitEvent(m_working, *m_selectedRow);
            m_selectedRow.reset();
            changed = true;
        }
        if (changed)
        {
            ApplyWorking();
        }
#endif
    }
} // namespace NS::Editor
