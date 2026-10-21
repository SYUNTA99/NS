#include "Editor/ActionTimelinePanel.h"

#include "Editor/ActionTimelineEdit.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/App/Application.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // 事象の種類の並び。HitReactionItem の選択肢の順で、種類を足すとここにも出る
            const std::vector<HitReactionItem>& ItemTypes()
            {
                static const std::vector<HitReactionItem> s_types = []<std::size_t... I>(std::index_sequence<I...>) {
                    return std::vector<HitReactionItem>{HitReactionItem{std::in_place_index<I>}...};
                }(std::make_index_sequence<std::variant_size_v<HitReactionItem>>{});
                return s_types;
            }

            // 入り切りの行に出す名前
            std::string SensorLabel(const ActionSensorEvent& sensor)
            {
                std::string label = "体のセンサー";
                if (sensor.attack)
                {
                    label = "攻撃のセンサー";
                }
                if (sensor.valid)
                {
                    return label + "を入れる";
                }
                return label + "を外す";
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

        ActionTimelinePanel::ActionTimelinePanel()
        {
            m_playback.speed = k_QuarterSpeed;
            // 開いた最初のフレームから、自機に一番近い相手への真ん中の当たりを見せる
            m_needsRun = true;
        }

        const ActorAction* ActionTimelinePanel::EditingAction() const noexcept
        {
            if (m_actionIndex >= m_actions.size())
            {
                return nullptr;
            }
            return &m_actions[m_actionIndex];
        }

        ActorAction* ActionTimelinePanel::WorkingAction() noexcept
        {
            if (m_actionIndex >= m_actions.size())
            {
                return nullptr;
            }
            return &m_actions[m_actionIndex];
        }

        void ActionTimelinePanel::LoadWorking(std::string_view name, std::string_view action)
        {
            m_actorFile = std::string{name};
            m_actions.clear();
            m_actionIndex = 0;
            m_selectedRow.reset();
            // 帯の時計の 0 は開いた動作で決まるので、下見の場面の範囲から取り直す
            m_needsRun = true;
            const nlohmann::json* document = ActorFileLibrary::Get().Find(m_actorFile);
            if (document == nullptr)
            {
                return;
            }
            m_actions = ReadActorActions(m_actorFile, *document);
            for (std::size_t i = 0; i < m_actions.size(); ++i)
            {
                if (m_actions[i].name == action)
                {
                    m_actionIndex = i;
                }
            }
        }

        void ActionTimelinePanel::FollowSelection(LevelEditorController& editor)
        {
            const SceneActor* selected = editor.SelectedObjectActor();
            if (selected == nullptr || selected->ActorFileName() == m_actorFile)
            {
                return;
            }
            if (ActorFileLibrary::Get().Find(selected->ActorFileName()) == nullptr)
            {
                return;
            }
            LoadWorking(selected->ActorFileName(), "");
        }

        void ActionTimelinePanel::ApplyWorking(LevelEditorController& editor)
        {
            ActorFileLibrary& library = ActorFileLibrary::Get();
            const nlohmann::json* current = library.Find(m_actorFile);
            if (current == nullptr)
            {
                m_status = std::format("{} の .actor が読まれていないので、変更を書けなかった", m_actorFile);
                return;
            }
            nlohmann::json document = *current;
            WriteActorActions(document, m_actions);
            if (!library.Set(m_actorFile, std::move(document)))
            {
                m_status = std::format("{} の .actor へ変更を書けなかった", m_actorFile);
                return;
            }
            m_dirty.insert(m_actorFile);
            // 動作は作る時に .actor から写すので、同じ .actor のアクターに読み直させてプレイにも効かせる
            for (SceneActor* actor : editor.Objects())
            {
                if (actor->ActorFileName() == m_actorFile && actor->ActionTimelineClassObj() != nullptr)
                {
                    actor->ActionTimelineClassObj()->ReloadActions();
                }
            }
            m_needsRun = true;
        }

        int ActionTimelinePanel::PlaybackOrigin() const noexcept
        {
            if (const ActorAction* action = EditingAction())
            {
                if (const std::optional<int> origin = ActionOriginIndex(m_result, action->name))
                {
                    return *origin;
                }
            }
            return m_result.detectionIndex;
        }

        TimelineFrameRange ActionTimelinePanel::PlaybackRange() const noexcept
        {
            if (!m_result.hit || m_result.frames.empty())
            {
                return TimelineFrameRange{0, -1};
            }
            const int origin = PlaybackOrigin();
            return TimelineFrameRange{-origin, static_cast<int>(m_result.frames.size()) - 1 - origin};
        }

        void ActionTimelinePanel::RunPreview(LevelEditorController& editor)
        {
            m_needsRun = false;
            m_snapshot = editor.SceneSnapshot();
            HitPreviewWorld world;
            if (Application* app = Application::Get())
            {
                world.device = &app->Device();
            }
            const bool wasHit = m_result.hit;
            HitPreviewDesc desc = m_desc;
            // 手入力の長さが過大でも、取り直しを無制限に回さないための上限
            constexpr int k_MaxPreviewFrames = 10000;
            for (const ActorAction& action : m_actions)
            {
                const int64 end =
                    static_cast<int64>(ActionLength(action)) + desc.leadFrames + desc.framesAfterRebound;
                desc.maxFrames =
                    std::max(desc.maxFrames, static_cast<int>(std::clamp(end, int64{0}, int64{k_MaxPreviewFrames})));
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

        void ActionTimelinePanel::ResetPreview() noexcept
        {
            m_preview.Clear();
            m_playback.playing = false;
            m_playback.carry = 0.0f;
            m_needsRun = true;
        }

        TimelinePreview* ActionTimelinePanel::Preview(LevelEditorController& editor)
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

        void ActionTimelinePanel::Tick(LevelEditorController& editor, float seconds)
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

        void ActionTimelinePanel::Render(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
            m_panelVisible = ImGui::Begin(k_PanelActionTimeline);
            if (m_panelVisible)
            {
                if (playMode)
                {
                    ImGui::TextUnformatted("プレイ中は下見しない。編集へ戻ると下見できる");
                }
                else
                {
                    FollowSelection(editor);
                    RenderConditions(editor);
                    ImGui::Separator();
                    RenderActionButtons(editor);
                    RenderPlayback();
                    RenderBands();
                    RenderSelectedRow(editor);
                }
            }
            ImGui::End();
#else
            (void)editor;
#endif
        }

        void ActionTimelinePanel::RenderConditions(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            ImGui::SeparatorText("当てる条件");
            bool changed = false;

            SceneActor* selected = editor.SelectedObjectActor();
            const bool selectable =
                selected != nullptr && !editor.SelectedIsPlayerObject() && selected->BodySensorClassObj() != nullptr;
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
                const GL::Level::ImpactRecord& impact = m_result.impact;
                ImGui::Text("当たった束: %s / 面の位置 (%.2f, %.2f) / 突進を出して %d フレーム目に検知 / 威力 %.2f",
                            impact.timeline.c_str(),
                            impact.faceU,
                            impact.faceV,
                            m_result.detectionIndex,
                            impact.power);
            }
            else if (!m_result.error.empty())
            {
                ImGui::TextColored(k_OverriddenFieldColor, "下見できない: %s", m_result.error.c_str());
            }
#else
            (void)editor;
#endif
        }

        void ActionTimelinePanel::RenderActionButtons(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            ImGui::SeparatorText("動作");
            if (const SceneActor* selected = editor.SelectedObjectActor())
            {
                if (ActorFileLibrary::Get().Find(selected->ActorFileName()) == nullptr)
                {
                    ImGui::TextColored(k_OverriddenFieldColor,
                                       "選んだアクターには .actor が無いので、%s の動作を出している",
                                       m_actorFile.c_str());
                }
            }
            if (m_actorFile.empty())
            {
                ImGui::TextUnformatted("シーンでアクターを選ぶと、その .actor の動作が出る");
                return;
            }
            ImGui::Text(".actor: %s", m_actorFile.c_str());
            RenderActionChoice(editor);
            ImGui::SameLine();
            RenderFileButtons(editor);
            RenderAddButtons(editor);
            if (!m_status.empty())
            {
                ImGui::TextUnformatted(m_status.c_str());
            }
#else
            (void)editor;
#endif
        }

        void ActionTimelinePanel::RenderActionChoice(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            std::string actionLabel = "(動作が無い)";
            if (const ActorAction* action = EditingAction())
            {
                actionLabel = action->name;
            }
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("動作", actionLabel.c_str()))
            {
                for (std::size_t i = 0; i < m_actions.size(); ++i)
                {
                    if (ImGui::Selectable(m_actions[i].name.c_str(), i == m_actionIndex))
                    {
                        m_actionIndex = i;
                        m_selectedRow.reset();
                        m_needsRun = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputTextWithHint("##new-name", "新しい動作の名前", m_newName, sizeof(m_newName));
            ImGui::SameLine();
            if (ImGui::Button("動作を足す"))
            {
                const std::string newName{m_newName};
                const bool taken = std::any_of(m_actions.begin(), m_actions.end(), [&newName](const ActorAction& other) {
                    return other.name == newName;
                });
                if (newName.empty() || taken)
                {
                    m_status = std::format("足せない名前: \"{}\"。空の名前と、同じ .actor にある名前は使えない", newName);
                }
                else
                {
                    ActorAction added;
                    added.name = newName;
                    m_actions.push_back(std::move(added));
                    m_actionIndex = m_actions.size() - 1;
                    m_selectedRow.reset();
                    m_newName[0] = '\0';
                    ApplyWorking(editor);
                }
            }
#else
            (void)editor;
#endif
        }

        void ActionTimelinePanel::RenderFileButtons(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            if (ImGui::Button("保存"))
            {
                ActorFileLibrary& library = ActorFileLibrary::Get();
                std::string failed;
                for (const std::string& name : m_dirty)
                {
                    if (!library.Save(name))
                    {
                        failed += std::format(" {}.actor", name);
                    }
                }
                if (failed.empty())
                {
                    m_status = std::format("{} へ変更した .actor を {} 個書いた", library.Directory(), m_dirty.size());
                    m_dirty.clear();
                }
                else
                {
                    m_status = std::format("書けなかった:{}", failed);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("元に戻す"))
            {
                // 置き場ごと読み直すので、ほかの .actor の保存していない変更も戻る
                ActorFileLibrary::Get().Reload();
                for (SceneActor* actor : editor.Objects())
                {
                    if (m_dirty.contains(actor->ActorFileName()) && actor->ActionTimelineClassObj() != nullptr)
                    {
                        actor->ActionTimelineClassObj()->ReloadActions();
                    }
                }
                std::string actionName;
                if (const ActorAction* action = EditingAction())
                {
                    actionName = action->name;
                }
                LoadWorking(m_actorFile, actionName);
                m_dirty.clear();
                m_needsRun = true;
                m_status = "最後に保存した中身へ戻した (全部の .actor)";
            }
            if (!m_dirty.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(k_OverriddenFieldColor, "保存していない変更がある");
            }
#else
            (void)editor;
#endif
        }

        void ActionTimelinePanel::RenderAddButtons(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            ImGui::BeginDisabled(EditingAction() == nullptr);
            const std::vector<HitReactionItem>& types = ItemTypes();
            m_addType = std::clamp(m_addType, 0, static_cast<int>(types.size()) - 1);
            ImGui::SetNextItemWidth(160.0f);
            const std::string currentLabel{HitReactionItemLabel(types[static_cast<std::size_t>(m_addType)])};
            if (ImGui::BeginCombo("##add-type", currentLabel.c_str()))
            {
                for (std::size_t i = 0; i < types.size(); ++i)
                {
                    const std::string label{HitReactionItemLabel(types[i])};
                    if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == m_addType))
                    {
                        m_addType = static_cast<int>(i);
                    }
                }
                ImGui::EndCombo();
            }
            int clock = 0;
            if (m_result.hit)
            {
                clock = std::max(m_playback.frame, 0);
            }
            ImGui::SameLine();
            if (ImGui::Button("再生の位置に足す"))
            {
                if (ActorAction* working = WorkingAction())
                {
                    m_selectedRow = AddActionEvent(*working, types[static_cast<std::size_t>(m_addType)], clock);
                    ApplyWorking(editor);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("入り切りを足す"))
            {
                if (ActorAction* working = WorkingAction())
                {
                    ActionSensorEvent sensor;
                    sensor.start = clock;
                    working->sensors.push_back(sensor);
                    m_selectedRow = working->events.size() + working->sensors.size() - 1;
                    ApplyWorking(editor);
                }
            }
            ImGui::EndDisabled();
#else
            (void)editor;
#endif
        }

        void ActionTimelinePanel::RenderPlayback() noexcept
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
            ImGui::TextUnformatted(
                "動作に入った歩を 0 としたフレーム。下見で動作が走らなければ検知が 0。Game ビューで動きを確認");
#endif
        }

        void ActionTimelinePanel::RenderBands() noexcept
        {
#if NS_EDITOR_ENABLED
            const ActorAction* action = EditingAction();
            if (action == nullptr)
            {
                return;
            }
            const HitPreviewResult* preview = nullptr;
            if (m_result.hit)
            {
                preview = &m_result;
            }
            TimelineFrameRange range = ActionFrameRange(*action, preview);
            const TimelineFrameRange playback = PlaybackRange();
            if (playback.first <= playback.last)
            {
                range.first = std::min(range.first, playback.first);
                range.last = std::max(range.last, playback.last);
            }
            std::vector<TimelineTrack> tracks;
            tracks.reserve(action->events.size() + action->sensors.size());
            for (std::size_t row = 0; row < action->events.size(); ++row)
            {
                const ActorActionEvent& event = action->events[row];
                TimelineTrack track;
                track.label = std::string{HitReactionItemLabel(event.item)};
                track.start = event.start;
                track.length = event.length;
                if (preview != nullptr)
                {
                    track.startedFrames = RowStartFrames(*preview, action->name, row);
                }
                tracks.push_back(std::move(track));
            }
            for (const ActionSensorEvent& sensor : action->sensors)
            {
                TimelineTrack track;
                track.label = SensorLabel(sensor);
                track.start = sensor.start;
                track.length = sensor.length;
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
                playback);
            if (moved)
            {
                m_showPreview = true;
                ImGui::SetWindowFocus(k_PanelGame);
            }
#endif
        }

        void ActionTimelinePanel::RenderPreviewGraphs(ImDrawList& draw, const TimelineLayout& layout) noexcept
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
                largestOffset =
                    std::max({largestOffset, std::fabs(frame.shakeOffset.x), std::fabs(frame.shakeOffset.y)});
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
            const int origin = PlaybackOrigin();
            // 1 本の折れ線。value は 0〜1 (center が真なら -1〜1) に写した値を返す
            const auto drawLine = [&](const ImVec2& at,
                                      ImU32 color,
                                      bool centered,
                                      const std::function<float(const HitPreviewFrame&)>& value) {
                ImVec2 previous{};
                for (std::size_t i = 0; i < frames.size(); ++i)
                {
                    const int clock = static_cast<int>(i) - origin;
                    const float x = at.x + (static_cast<float>(clock - firstClock) + 0.5f) * frameWidth;
                    float ratio = value(frames[i]);
                    if (centered)
                    {
                        ratio = ratio * 0.5f + 0.5f;
                    }
                    const float y = at.y + graphHeight - 2.0f - std::clamp(ratio, 0.0f, 1.0f) * (graphHeight - 4.0f);
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
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2{totalWidth, graphHeight});
                    draw.AddRectFilled(at, ImVec2{at.x + totalWidth, at.y + graphHeight}, k_GraphBackColor);
                    body(at);
                };
            graphRow("トラウマ", "カメラのトラウマ。下が 0、上が 1", [&](const ImVec2& at) {
                drawLine(at, k_GraphTraumaColor, false, [](const HitPreviewFrame& frame) { return frame.trauma; });
            });
            graphRow("揺れの角度",
                     "トラウマの揺れの角度。赤が横の首振り、緑が縦の首振り、青が傾き。真ん中が 0 "
                     "で、一番大きい所で縦を合わせる",
                     [&](const ImVec2& at) {
                         drawLine(at, k_GraphXColor, true, [largestAngle](const HitPreviewFrame& frame) {
                             return frame.shakeAngles.x / largestAngle;
                         });
                         drawLine(at, k_GraphYColor, true, [largestAngle](const HitPreviewFrame& frame) {
                             return frame.shakeAngles.y / largestAngle;
                         });
                         drawLine(at, k_GraphZColor, true, [largestAngle](const HitPreviewFrame& frame) {
                             return frame.shakeAngles.z / largestAngle;
                         });
                     });
            graphRow("揺れのずれ",
                     "平行移動の揺れのずれ。赤が右、緑が上。真ん中が 0 で、一番大きい所で縦を合わせる",
                     [&](const ImVec2& at) {
                         drawLine(at, k_GraphXColor, true, [largestOffset](const HitPreviewFrame& frame) {
                             return frame.shakeOffset.x / largestOffset;
                         });
                         drawLine(at, k_GraphYColor, true, [largestOffset](const HitPreviewFrame& frame) {
                             return frame.shakeOffset.y / largestOffset;
                         });
                     });
            graphRow("沈む揺れ",
                     "沈む揺れの画面の縦のずれ。真ん中が 0 で下が沈み、一番大きい所で縦を合わせる",
                     [&](const ImVec2& at) {
                         drawLine(at, k_GraphYColor, true, [largestSink](const HitPreviewFrame& frame) {
                             return frame.sinkPixels / largestSink;
                         });
                     });
            graphRow("世界の速さ", "世界の速さ。下が 0、上が普段の速さ 1", [&](const ImVec2& at) {
                drawLine(at, k_GraphSpeedColor, false, [](const HitPreviewFrame& frame) { return frame.worldSpeed; });
            });
#else
            (void)draw;
            (void)layout;
#endif
        }

        void ActionTimelinePanel::RenderSelectedEvent(ActorActionEvent& event, bool& changed) noexcept
        {
#if NS_EDITOR_ENABLED
            const std::string title{HitReactionItemLabel(event.item)};
            ImGui::SeparatorText(title.c_str());
            if (BeginFieldTable("##event-row"))
            {
                FieldRow("始まり");
                int start = event.start;
                if (ImGui::DragInt("##start", &start, 0.2f, 0, 600))
                {
                    event.start = std::max(start, 0);
                    changed = true;
                }
                FieldRow("長さ");
                int length = event.length;
                if (ImGui::DragInt("##length", &length, 0.2f, 0, 600))
                {
                    event.length = std::max(length, 0);
                    changed = true;
                }
                EndFieldTable();
            }
            if (const ReflectionInfo* info = HitReactionItemReflection(event.item))
            {
                const ValueEditResult fields = DrawReflectedValue(HitReactionItemFields(event.item), *info);
                changed |= fields.changed;
            }
#else
            (void)event;
            (void)changed;
#endif
        }

        void ActionTimelinePanel::RenderSelectedSensor(ActionSensorEvent& sensor, bool& changed) noexcept
        {
#if NS_EDITOR_ENABLED
            ImGui::SeparatorText("センサーの入り切り");
            if (BeginFieldTable("##event-row"))
            {
                FieldRow("センサー");
                int kind = 0;
                if (sensor.attack)
                {
                    kind = 1;
                }
                if (ImGui::Combo("##sensor", &kind, "体のセンサー\0攻撃のセンサー\0"))
                {
                    sensor.attack = kind == 1;
                    changed = true;
                }
                FieldRow("長さの間");
                int valid = 0;
                if (sensor.valid)
                {
                    valid = 1;
                }
                if (ImGui::Combo("##valid", &valid, "外す\0入れる\0"))
                {
                    sensor.valid = valid == 1;
                    changed = true;
                }
                FieldRow("始まり");
                int start = sensor.start;
                if (ImGui::DragInt("##start", &start, 0.2f, 0, 600))
                {
                    sensor.start = std::max(start, 0);
                    changed = true;
                }
                FieldRow("長さ");
                int length = sensor.length;
                if (ImGui::DragInt("##length", &length, 0.2f, 0, 600))
                {
                    sensor.length = std::max(length, 0);
                    changed = true;
                }
                EndFieldTable();
            }
#else
            (void)sensor;
            (void)changed;
#endif
        }

        void ActionTimelinePanel::RenderSelectedRow(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            ActorAction* action = WorkingAction();
            if (action == nullptr || !m_selectedRow.has_value())
            {
                return;
            }
            const std::size_t row = *m_selectedRow;
            bool changed = false;
            if (row < action->events.size())
            {
                RenderSelectedEvent(action->events[row], changed);
                if (ImGui::Button("この事象を消す"))
                {
                    (void)RemoveActionEvent(*action, row);
                    m_selectedRow.reset();
                    changed = true;
                }
            }
            else if (row - action->events.size() < action->sensors.size())
            {
                const std::size_t sensorRow = row - action->events.size();
                RenderSelectedSensor(action->sensors[sensorRow], changed);
                if (ImGui::Button("この入り切りを消す"))
                {
                    action->sensors.erase(action->sensors.begin() + static_cast<std::ptrdiff_t>(sensorRow));
                    m_selectedRow.reset();
                    changed = true;
                }
            }
            else
            {
                m_selectedRow.reset();
            }
            if (changed)
            {
                ApplyWorking(editor);
            }
#else
            (void)editor;
#endif
        }
    } // namespace Editor
} // namespace NS
