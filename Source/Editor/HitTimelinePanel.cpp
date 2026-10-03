#include "Editor/HitTimelinePanel.h"

#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/App/Application.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Input.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
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
        template <std::size_t I> void CollectEventTypes(std::vector<HitEventValue>& out)
        {
            if constexpr (I < std::variant_size_v<HitEventValue>)
            {
                out.emplace_back(std::in_place_index<I>);
                CollectEventTypes<I + 1>(out);
            }
        }

        const std::vector<HitEventValue>& EventTypes()
        {
            static const std::vector<HitEventValue> s_types = [] {
                std::vector<HitEventValue> types;
                CollectEventTypes<0>(types);
                return types;
            }();
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

        // 横から見る絵の、当たった点からの距離 (m) と高さ (m)。相手と自機が 1 枚に収まる大きさ
        constexpr float k_SideViewDistance = 7.0f;
        constexpr float k_SideViewHeight = 1.0f;
        constexpr float k_SideViewFovDegrees = 50.0f;

        // 再生の速さ。12 フレームの止めはそのままの速さだと 0.2 秒で目で追えないので、遅い方を既定にする
        constexpr float k_NormalSpeed = 1.0f;
        constexpr float k_QuarterSpeed = 0.25f;

        // 帯の行の左の名前の幅 (px)
        constexpr float k_BandLabelWidth = 170.0f;
        // 1 フレームの帯の幅の下限 (px)。狭いと押せない
        constexpr float k_MinFrameWidth = 4.0f;

#if NS_EDITOR_ENABLED
        constexpr ImU32 k_BandColor = IM_COL32(90, 140, 210, 255);
        constexpr ImU32 k_BandSelectedColor = IM_COL32(240, 170, 60, 255);
        constexpr ImU32 k_StartedColor = IM_COL32(250, 230, 90, 255);
        constexpr ImU32 k_PlayheadColor = IM_COL32(255, 80, 80, 255);
        constexpr ImU32 k_DetectionColor = IM_COL32(200, 200, 200, 120);
        constexpr ImU32 k_RulerTextColor = IM_COL32(200, 200, 200, 255);
        constexpr int k_PreviewGraphCount = 4; // 帯の下の折れ線の行の数
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

    HitTimelinePanel::~HitTimelinePanel()
    {
        DropPreview();
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
        m_hasSelectedRow = false;
    }

    void HitTimelinePanel::ApplyWorking()
    {
        NS::Game::Level::HitTimelineLibrary::Get().Set(NS::Game::Level::HitTimelineNameOf(m_tier), m_working);
        m_dirty = true;
        m_needsRun = true;
    }

    void HitTimelinePanel::DropPreview() noexcept
    {
        // 自機の HitReaction は片付けでパッドへ 0 を書くので、手元のパッドへ書かせないよう中立の中で壊す
        const NS::Platform::ScopedNeutralInput neutral;
        m_scene.reset();
        m_sceneFrame = -1;
    }

    void HitTimelinePanel::RunPreview(LevelEditorController& editor)
    {
        m_needsRun = false;
        DropPreview();
        m_snapshot = editor.SceneSnapshot();
        HitPreviewWorld world;
        if (NS::App::Application* app = NS::App::Application::Get())
        {
            world.assets = &app->Assets();
            world.renderer = &app->Renderer();
        }
        const bool wasHit = m_result.hit;
        m_result = RunHitPreview(m_snapshot, m_desc, world);
        // 選んでいなかった相手は下見が選んだ物に固定し、条件を変えても同じ相手を見続ける
        m_desc.targetId = m_result.desc.targetId;
        const int last = std::max(static_cast<int>(m_result.frames.size()) - 1, 0);
        // 初めて当たった下見は当たりの瞬間から見せる。値を変えて下見し直した時は、見ていたフレームのまま比べられるよう動かさない
        if (m_result.hit && (!wasHit || m_playback.frame > last))
        {
            m_playback.frame = m_result.detectionIndex;
        }
    }

    void HitTimelinePanel::PrepareSceneAt(int frameIndex)
    {
        if (m_scene != nullptr && m_sceneFrame <= frameIndex)
        {
            if (frameIndex > m_sceneFrame)
            {
                m_scene->SetSimulationPaused(false);
                StepHitPreviewScene(*m_scene, frameIndex - m_sceneFrame);
                m_sceneFrame = frameIndex;
            }
        }
        else
        {
            DropPreview();
            HitPreviewWorld world;
            if (NS::App::Application* app = NS::App::Application::Get())
            {
                world.assets = &app->Assets();
                world.renderer = &app->Renderer();
            }
            m_scene = BuildHitPreviewSceneAt(m_snapshot, m_result, frameIndex, world);
            m_sceneFrame = frameIndex;
        }
        // 描く間は止めておく。止めていないと描くたびに前の固定フレームとの補間の割合が変わり、同じフレームの絵が揺れる
        if (m_scene != nullptr)
        {
            m_scene->SetSimulationPaused(true);
        }
    }

    void HitTimelinePanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
        if (ImGui::Begin(k_PanelHitTimeline))
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

        if (playMode)
        {
            DropPreview();
            Suppress();
            return;
        }
        if (m_needsRun && !ImGui::IsAnyItemActive())
        {
            RunPreview(editor);
        }
        const int last = std::max(static_cast<int>(m_result.frames.size()) - 1, 0);
        m_playback.Tick(ImGui::GetIO().DeltaTime, last);
        if (m_result.hit)
        {
            PrepareSceneAt(m_playback.frame);
        }
        RenderImages();
#else
        (void)editor;
#endif
    }

    void HitTimelinePanel::Suppress() noexcept
    {
        m_gameSurface.ResetVisibility();
        m_sideSurface.ResetVisibility();
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
        changed |= ImGui::SliderFloat("溜め (0 はタップ)", &m_desc.charge01, 0.0f, 1.0f, "%.2f");
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
            for (const HitTier tier : NS::Game::Level::k_AllHitTiers)
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
                clock = m_playback.frame - m_result.detectionIndex;
            }
            m_selectedRow = AddHitEvent(m_working, types[static_cast<std::size_t>(m_addType)], clock);
            m_hasSelectedRow = true;
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
        const int last = std::max(static_cast<int>(m_result.frames.size()) - 1, 0);
        if (ImGui::Button("|<"))
        {
            m_playback.StepBy(-last - 1, last);
        }
        ImGui::SameLine();
        if (ImGui::Button("<"))
        {
            m_playback.StepBy(-1, last);
        }
        ImGui::SameLine();
        const char* playLabel = "再生";
        if (m_playback.playing)
        {
            playLabel = "止める";
        }
        if (ImGui::Button(playLabel))
        {
            if (m_playback.playing)
            {
                m_playback.playing = false;
            }
            else
            {
                // 最後で押したら頭から
                if (m_playback.frame >= last)
                {
                    m_playback.frame = 0;
                }
                m_playback.playing = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(">"))
        {
            m_playback.StepBy(1, last);
        }
        ImGui::SameLine();
        bool quarter = m_playback.speed < 1.0f;
        if (ImGui::Checkbox("1/4 の速さ", &quarter))
        {
            m_playback.speed = k_NormalSpeed;
            if (quarter)
            {
                m_playback.speed = k_QuarterSpeed;
            }
        }
        ImGui::SameLine();
        int clock = 0;
        if (m_result.hit)
        {
            clock = m_playback.frame - m_result.detectionIndex;
        }
        ImGui::Text("フレーム %d (検知から %+d)", m_playback.frame, clock);
        ImGui::SameLine();
        // 横の向きに別の配置物があると相手が隠れるので、反対側から見られるようにする
        ImGui::Checkbox("横の絵を反対側から", &m_sideFlipped);
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
        const HitPreviewFrameRange range = HitTimelineFrameRange(m_working, preview);
        const int frameCount = range.last - range.first + 1;
        const float rowHeight = ImGui::GetFrameHeight();
        const float bandsWidth = std::max(ImGui::GetContentRegionAvail().x - k_BandLabelWidth, 1.0f);
        const float frameWidth = std::max(bandsWidth / static_cast<float>(frameCount), k_MinFrameWidth);
        const float totalWidth = frameWidth * static_cast<float>(frameCount);
        // 帯は横に流れる区画へ入れ、フレームが多い時は横に送れるようにする
        // 帯の行は 360 で頭打ちにして縦に送る。下見の折れ線の 4 行はその下へいつも見える高さを足す
        float graphsHeight = 0.0f;
        if (m_result.hit && !m_result.frames.empty())
        {
            graphsHeight =
                static_cast<float>(k_PreviewGraphCount) * (rowHeight * 2.0f + ImGui::GetStyle().ItemSpacing.y);
        }
        // 行ごとに行の間の空きが入る
        const float regionHeight =
            (rowHeight + ImGui::GetStyle().ItemSpacing.y) * static_cast<float>(m_working.events.size() + 1) +
            graphsHeight + ImGui::GetStyle().ScrollbarSize + ImGui::GetStyle().ItemSpacing.y * 2.0f;
        if (!ImGui::BeginChild("##bands",
                               ImVec2{0.0f, std::min(regionHeight, 360.0f + graphsHeight)},
                               ImGuiChildFlags_Borders,
                               ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::EndChild();
            return;
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const float bandsLeft = top.x + k_BandLabelWidth;
        const bool overlay = m_result.hit && m_result.impact.tier == m_tier;

        // 目盛り。押すとそのフレームへ再生の位置を動かす
        ImGui::Dummy(ImVec2{k_BandLabelWidth, rowHeight});
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##ruler", ImVec2{totalWidth, rowHeight});
        if (ImGui::IsItemActive() && m_result.hit)
        {
            const int clock = range.first + static_cast<int>((ImGui::GetIO().MousePos.x - bandsLeft) / frameWidth);
            const int last = std::max(static_cast<int>(m_result.frames.size()) - 1, 0);
            m_playback.playing = false;
            m_playback.frame = std::clamp(clock + m_result.detectionIndex, 0, last);
        }
        for (int clock = range.first; clock <= range.last; ++clock)
        {
            if (clock % 5 != 0)
            {
                continue;
            }
            const float x = bandsLeft + static_cast<float>(clock - range.first) * frameWidth;
            char text[16];
            std::snprintf(text, sizeof(text), "%d", clock);
            draw->AddLine(ImVec2{x, top.y + rowHeight * 0.6f}, ImVec2{x, top.y + rowHeight}, k_RulerTextColor);
            draw->AddText(ImVec2{x + 2.0f, top.y}, k_RulerTextColor, text);
        }

        for (std::size_t row = 0; row < m_working.events.size(); ++row)
        {
            const HitEvent& event = m_working.events[row];
            ImGui::PushID(static_cast<int>(row));
            std::string label{NS::Game::Level::HitEventLabel(event.value)};
            if (event.direction != HitDirection::Any)
            {
                label += std::format(" ({})", DirectionLabel(event.direction));
            }
            const bool selected = m_hasSelectedRow && m_selectedRow == row;
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2{k_BandLabelWidth - 4.0f, rowHeight}))
            {
                m_selectedRow = row;
                m_hasSelectedRow = true;
            }
            ImGui::SameLine(k_BandLabelWidth, 0.0f);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##band", ImVec2{totalWidth, rowHeight}))
            {
                m_selectedRow = row;
                m_hasSelectedRow = true;
            }
            const float x0 = rowMin.x + static_cast<float>(event.start - range.first) * frameWidth;
            const float x1 = x0 + static_cast<float>(std::max(event.length, 1)) * frameWidth;
            ImU32 color = k_BandColor;
            if (selected)
            {
                color = k_BandSelectedColor;
            }
            draw->AddRectFilled(ImVec2{x0, rowMin.y + 2.0f}, ImVec2{x1, rowMin.y + rowHeight * 0.65f}, color, 2.0f);
            // 下見で実際に始まったフレームを、帯の下に細い印で重ねる
            if (overlay)
            {
                for (const int clock : RowStartFrames(m_result, row))
                {
                    const float sx = rowMin.x + static_cast<float>(clock - range.first) * frameWidth;
                    draw->AddRectFilled(ImVec2{sx, rowMin.y + rowHeight * 0.72f},
                                        ImVec2{sx + std::max(frameWidth, 3.0f), rowMin.y + rowHeight - 1.0f},
                                        k_StartedColor);
                }
            }
            ImGui::PopID();
        }

        // 下見の揺れ・トラウマ・世界の速さを、帯と同じフレームの並びで折れ線に重ねる。段が違っても下見そのものの値
        if (m_result.hit && !m_result.frames.empty())
        {
            RenderPreviewGraphs(*draw, range.first, frameWidth, totalWidth, rowHeight);
        }

        // 検知のフレームと再生の位置の縦線
        const float bottom = ImGui::GetCursorScreenPos().y;
        const float detectionX = bandsLeft + static_cast<float>(0 - range.first) * frameWidth;
        draw->AddLine(ImVec2{detectionX, top.y}, ImVec2{detectionX, bottom}, k_DetectionColor, 1.0f);
        if (m_result.hit)
        {
            const int clock = m_playback.frame - m_result.detectionIndex;
            const float x = bandsLeft + (static_cast<float>(clock - range.first) + 0.5f) * frameWidth;
            draw->AddLine(ImVec2{x, top.y}, ImVec2{x, bottom}, k_PlayheadColor, 2.0f);
        }
        ImGui::EndChild();
#endif
    }

    void HitTimelinePanel::RenderPreviewGraphs(
        ImDrawList& draw, int firstClock, float frameWidth, float totalWidth, float rowHeight) noexcept
    {
#if NS_EDITOR_ENABLED
        const std::vector<HitPreviewFrame>& frames = m_result.frames;
        // 揺れの角度とずれは一番大きい所で縦を合わせる。0 しか無ければ 1 で割る
        float largestAngle = 0.0f;
        float largestOffset = 0.0f;
        for (const HitPreviewFrame& frame : frames)
        {
            largestAngle = std::max({largestAngle,
                                     std::fabs(frame.shakeAngles.x),
                                     std::fabs(frame.shakeAngles.y),
                                     std::fabs(frame.shakeAngles.z)});
            largestOffset = std::max({largestOffset, std::fabs(frame.shakeOffset.x), std::fabs(frame.shakeOffset.y)});
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
                ImGui::SameLine(k_BandLabelWidth, 0.0f);
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
        graphRow("世界の速さ", "世界の速さ。下が 0、上が普段の速さ 1", [&](const ImVec2& origin) {
            drawLine(origin, k_GraphSpeedColor, false, [](const HitPreviewFrame& frame) { return frame.worldSpeed; });
        });
#else
        (void)draw;
        (void)firstClock;
        (void)frameWidth;
        (void)totalWidth;
        (void)rowHeight;
#endif
    }

    void HitTimelinePanel::RenderSelectedRow() noexcept
    {
#if NS_EDITOR_ENABLED
        if (!m_hasSelectedRow || m_selectedRow >= m_working.events.size())
        {
            return;
        }
        HitEvent& event = m_working.events[m_selectedRow];
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
            (void)RemoveHitEvent(m_working, m_selectedRow);
            m_hasSelectedRow = false;
            changed = true;
        }
        if (changed)
        {
            ApplyWorking();
        }
#endif
    }

    void HitTimelinePanel::RenderImages() noexcept
    {
#if NS_EDITOR_ENABLED
        ImVec2 rectMin{};
        ImVec2 rectMax{};
        bool hovered = false;
        (void)m_gameSurface.BeginView(k_PanelHitPreviewGame, rectMin, rectMax, hovered);
        m_gameSurface.EndView();
        (void)m_sideSurface.BeginView(k_PanelHitPreviewSide, rectMin, rectMax, hovered);
        m_sideSurface.EndView();
#endif
    }

    void HitTimelinePanel::RenderPreview() noexcept
    {
        if (m_scene == nullptr || !m_result.hit)
        {
            return;
        }
        std::vector<NS::Obj::SceneView> views;
        // ゲームのカメラの絵は、下見の場面のカメラの管理役が選ぶカメラ (揺れと寄りを掛けた後)
        if (std::optional<NS::Obj::SceneView> game = m_gameSurface.CollectView(std::nullopt))
        {
            views.push_back(*game);
        }
        // 横の絵は、突進の向きに直交する横から当たった点を見る
        NS::Obj::CameraPose side;
        const NS::Core::Vector3 point = m_result.impact.surfacePoint;
        NS::Core::Vector3 across{m_result.direction.z, 0.0f, -m_result.direction.x};
        if (m_sideFlipped)
        {
            across = -across;
        }
        side.target = point;
        side.position = point + across * k_SideViewDistance + NS::Core::Vector3{0.0f, k_SideViewHeight, 0.0f};
        side.fovY = NS::Core::ToRadians(NS::Core::Degrees{k_SideViewFovDegrees});
        if (std::optional<NS::Obj::SceneView> sideView = m_sideSurface.CollectView(side))
        {
            views.push_back(*sideView);
        }
        if (views.empty())
        {
            return;
        }
        m_scene->SetSceneViews(std::move(views));
        m_scene->OnRender();
        m_scene->SetSceneViews({});
    }

    void HitTimelinePanel::ReleaseTargets() noexcept
    {
        DropPreview();
        m_gameSurface.Release();
        m_sideSurface.Release();
    }
} // namespace NS::Editor
