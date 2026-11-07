#include "Editor/ActionTimelinePanel.h"

#include "Editor/ActionTimelineEdit.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ItemVariant.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <format>
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <imgui.h>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // 項目の種類の並び。ActionItem の選択肢の順で、種類を足すとここにも出る
            const std::vector<ActionItem>& ItemTypes()
            {
                static const std::vector<ActionItem> s_types = []<std::size_t... I>(std::index_sequence<I...>) {
                    return std::vector<ActionItem>{ActionItem{std::in_place_index<I>}...};
                }(std::make_index_sequence<std::variant_size_v<ActionItem>>{});
                return s_types;
            }

            std::string SensorLabel(const ActionSensorEvent& sensor)
            {
                if (sensor.valid)
                {
                    return sensor.sensor + " を入れる";
                }
                return sensor.sensor + " を外す";
            }

            // 直している .actor actorFile を元に持つ actor のセンサー。作った順
            // actor が nullptr か別の .actor を指す時は空。窓は選びを外しても前の .actor の動作を出し続けるので、
            // 別のクラスのセンサーの名前を入り切りに書かない
            std::span<HitSensor* const> SensorsOf(const SceneActor* actor, const std::string& actorFile)
            {
                if (actor == nullptr || actor->HitSensorsClassObj() == nullptr ||
                    ActorFileToWrite(actor, FieldDestination::ActorFile) != actorFile)
                {
                    return {};
                }
                return actor->HitSensorsClassObj()->Sensors();
            }
        } // namespace

        const ActorActionInfo* ActionTimelinePanel::EditingAction() const noexcept
        {
            if (m_actionIndex >= m_actions.size())
            {
                return nullptr;
            }
            return &m_actions[m_actionIndex];
        }

        ActorActionInfo* ActionTimelinePanel::WorkingAction() noexcept
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
            const SceneActor* selected = editor.SelectedActor();
            if (selected == nullptr)
            {
                return;
            }
            // 動作は元の .actor だけが持つので、派生を指すアクターでも元の .actor を開く
            const std::string actorFile = ActorFileToWrite(selected, FieldDestination::ActorFile);
            if (actorFile == m_actorFile || ActorFileLibrary::Get().Find(actorFile) == nullptr)
            {
                return;
            }
            LoadWorking(actorFile, "");
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
            // 動作は作る時に .actor から写すので、この .actor と派生のアクターに読み直させてプレイにも効かせる
            for (SceneActor* actor : editor.Actors())
            {
                // 動作の部品が無いアクターでは ReloadActions が何もしない
                if (ActorFileToWrite(actor, FieldDestination::ActorFile) == m_actorFile)
                {
                    actor->ReloadActions();
                }
            }
        }

        bool ActionTimelinePanel::SaveAll() noexcept
        {
            ActorFileLibrary& library = ActorFileLibrary::Get();
            std::string changedOutside;
            std::string failed;
            for (const std::string& name : m_dirty)
            {
                if (library.Save(name))
                {
                    continue;
                }
                if (library.HasFileChangedOutside(name))
                {
                    changedOutside += std::format(" {}.actor", name);
                }
                else
                {
                    failed += std::format(" {}.actor", name);
                }
            }
            if (!changedOutside.empty())
            {
                m_status = std::format("外で変更されたので書かなかった:{}", changedOutside);
                if (!failed.empty())
                {
                    m_status += std::format(" / 書けなかった:{}", failed);
                }
                return false;
            }
            if (!failed.empty())
            {
                m_status = std::format("書けなかった:{}", failed);
                return false;
            }
            m_status = std::format("{} へ変更した .actor を {} 個書いた", library.Directory(), m_dirty.size());
            m_dirty.clear();
            return true;
        }

        void ActionTimelinePanel::Render(LevelEditorController& editor) noexcept
        {
            const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;
            if (ImGui::Begin(k_PanelActionTimeline))
            {
                if (playMode)
                {
                    ImGui::TextUnformatted("プレイ中は動作を直せない。編集へ戻ると直せる");
                }
                else
                {
                    FollowSelection(editor);
                    RenderActionButtons(editor);
                    RenderBands();
                    RenderSelectedRow(editor);
                }
            }
            ImGui::End();
        }

        void ActionTimelinePanel::RenderActionButtons(LevelEditorController& editor) noexcept
        {
            ImGui::SeparatorText("動作");
            if (const SceneActor* selected = editor.SelectedActor())
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
        }

        void ActionTimelinePanel::RenderActionChoice(LevelEditorController& editor) noexcept
        {
            std::string actionLabel = "(動作が無い)";
            if (const ActorActionInfo* action = EditingAction())
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
                    }
                }
                ImGui::EndCombo();
            }
            if (ActorActionInfo* working = WorkingAction())
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                int length = working->length;
                if (ImGui::DragInt("長さ", &length, 0.2f, 0, 600))
                {
                    working->length = std::max(length, 0);
                    ApplyWorking(editor);
                }
                ImGui::SetItemTooltip("%s",
                                      "状態の中で進める Step の回数。この回数だけ OnStep が進んだ次の OnStep で "
                                      "IsStateEnd が真になる。0 は長さを決めていない");
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputTextWithHint("##new-name", "新しい動作の名前", m_newName, sizeof(m_newName));
            ImGui::SameLine();
            if (ImGui::Button("動作を足す"))
            {
                const std::string newName{m_newName};
                const bool taken =
                    std::any_of(m_actions.begin(), m_actions.end(), [&newName](const ActorActionInfo& other) {
                        return other.name == newName;
                    });
                if (newName.empty() || taken)
                {
                    m_status =
                        std::format("足せない名前: \"{}\"。空の名前と、同じ .actor にある名前は使えない", newName);
                }
                else
                {
                    ActorActionInfo added;
                    added.name = newName;
                    m_actions.push_back(std::move(added));
                    m_actionIndex = m_actions.size() - 1;
                    m_selectedRow.reset();
                    m_newName[0] = '\0';
                    ApplyWorking(editor);
                }
            }
        }

        void ActionTimelinePanel::RevertDirtyActions(LevelEditorController& editor)
        {
            ActorFileLibrary& library = ActorFileLibrary::Get();
            std::string failed;
            const std::set<std::string, std::less<>> names = m_dirty;
            for (const std::string& name : names)
            {
                // 動作だけをファイルの中身へ戻す。書けずに再試行を待つ欄の編集は文書に残す
                // 読めないファイルは ReadActorFileAt が ERROR を出している。戻す先が無いので未保存のまま残す
                const std::optional<nlohmann::json> saved =
                    ReadActorFileAt(OS::FileSystem::Combine(library.Directory(), name + ".actor"));
                const nlohmann::json* current = library.Find(name);
                // 外で変更されたファイルの動作だけを文書へ混ぜない。外の変更は起動し直して読む
                if (!saved.has_value() || current == nullptr || library.HasFileChangedOutside(name))
                {
                    failed += std::format(" {}.actor", name);
                    continue;
                }
                nlohmann::json document = *current;
                nlohmann::json actions = nlohmann::json::array();
                if (const nlohmann::json* savedActions = ActorFileActions(*saved))
                {
                    actions = *savedActions;
                }
                SetActorFileActions(document, std::move(actions));
                if (!library.Set(name, std::move(document)))
                {
                    failed += std::format(" {}.actor", name);
                    continue;
                }
                m_dirty.erase(name);
                for (SceneActor* actor : editor.Actors())
                {
                    // m_dirty は動作を書く先の名前なので、派生を指すアクターも書く先の名前で比べる
                    if (ActorFileToWrite(actor, FieldDestination::ActorFile) == name)
                    {
                        actor->ReloadActions();
                    }
                }
            }
            std::string actionName;
            if (const ActorActionInfo* action = EditingAction())
            {
                actionName = action->name;
            }
            LoadWorking(m_actorFile, actionName);
            if (failed.empty())
            {
                m_status = "動作を最後に保存した中身へ戻した";
            }
            else
            {
                m_status = std::format("戻せなかった:{}", failed);
            }
        }

        void ActionTimelinePanel::RenderFileButtons(LevelEditorController& editor) noexcept
        {
            if (ImGui::Button("保存"))
            {
                (void)SaveAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("元に戻す"))
            {
                RevertDirtyActions(editor);
            }
            if (!m_dirty.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(k_OverriddenFieldColor, "保存していない変更がある");
            }
        }

        void ActionTimelinePanel::RenderAddButtons(LevelEditorController& editor) noexcept
        {
            ImGui::BeginDisabled(EditingAction() == nullptr);
            const std::vector<ActionItem>& types = ItemTypes();
            m_addType = std::clamp(m_addType, 0, static_cast<int>(types.size()) - 1);
            ImGui::SetNextItemWidth(160.0f);
            const std::string currentLabel{ItemLabel(types[static_cast<std::size_t>(m_addType)])};
            if (ImGui::BeginCombo("##add-type", currentLabel.c_str()))
            {
                for (std::size_t i = 0; i < types.size(); ++i)
                {
                    const std::string label{ItemLabel(types[i])};
                    if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == m_addType))
                    {
                        m_addType = static_cast<int>(i);
                    }
                }
                ImGui::EndCombo();
            }
            const int clock = std::max(m_playback.frame, 0);
            ImGui::SameLine();
            if (ImGui::Button("再生の位置に足す"))
            {
                if (ActorActionInfo* working = WorkingAction())
                {
                    m_selectedRow = AddActionEvent(*working, types[static_cast<std::size_t>(m_addType)], clock);
                    ApplyWorking(editor);
                }
            }
            ImGui::SameLine();
            // 入り切りはセンサーを名前で指す。名前を持たない入り切りは .actor
            // に書けないので、選んだアクターの最初の物を入れる
            const std::span<HitSensor* const> sensors = SensorsOf(editor.SelectedActor(), m_actorFile);
            ImGui::BeginDisabled(sensors.empty());
            if (ImGui::Button("入り切りを足す"))
            {
                if (ActorActionInfo* working = WorkingAction())
                {
                    ActionSensorEvent sensor;
                    sensor.sensor = sensors.front()->Name();
                    sensor.start = clock;
                    working->sensors.push_back(sensor);
                    m_selectedRow = working->events.size() + working->sensors.size() - 1;
                    ApplyWorking(editor);
                }
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }

        void ActionTimelinePanel::RenderBands() noexcept
        {
            const ActorActionInfo* action = EditingAction();
            if (action == nullptr)
            {
                return;
            }
            const TimelineFrameRange range = ActionFrameRange(*action);
            // 動作を替えて範囲が縮んでも、足す位置を帯の中に置く
            m_playback.Seek(m_playback.frame, range);
            ImGui::Text("足す位置: 動作に入った歩を 0 としたフレーム %d。目盛りを押して動かす", m_playback.frame);
            std::vector<TimelineTrack> tracks;
            tracks.reserve(action->events.size() + action->sensors.size());
            for (std::size_t row = 0; row < action->events.size(); ++row)
            {
                const ActorActionEvent& event = action->events[row];
                TimelineTrack track;
                track.label = std::string{ItemLabel(event.item)};
                track.start = event.start;
                track.length = event.length;
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
            DrawTimelineTracks(tracks, range, m_playback, m_selectedRow, true);
        }

        namespace
        {
            void DrawFrameSpanRows(int& outStart, int& outLength, bool& changed) noexcept
            {
                FieldRow("始まり");
                int start = outStart;
                if (ImGui::DragInt("##start", &start, 0.2f, 0, 600))
                {
                    outStart = std::max(start, 0);
                    changed = true;
                }
                FieldRow("長さ");
                int length = outLength;
                if (ImGui::DragInt("##length", &length, 0.2f, 0, 600))
                {
                    outLength = std::max(length, 0);
                    changed = true;
                }
            }

            void DrawBoolComboRow(
                const char* label, const char* id, bool& flag, const char* items, bool& changed) noexcept
            {
                FieldRow(label);
                int index = 0;
                if (flag)
                {
                    index = 1;
                }
                if (ImGui::Combo(id, &index, items))
                {
                    flag = index == 1;
                    changed = true;
                }
            }
        } // namespace

        void ActionTimelinePanel::RenderSelectedEvent(ActorActionEvent& event, bool& changed) noexcept
        {
            const std::string title{ItemLabel(event.item)};
            ImGui::SeparatorText(title.c_str());
            if (BeginFieldTable("##event-row"))
            {
                DrawFrameSpanRows(event.start, event.length, changed);
                EndFieldTable();
            }
            if (const ReflectionInfo* info = ItemReflection(event.item))
            {
                const ValueEditResult fields = DrawReflectedValue(ItemFields(event.item), *info);
                changed |= fields.changed;
            }
        }

        void ActionTimelinePanel::RenderSelectedSensor(const SceneActor* selected,
                                                       ActionSensorEvent& sensor,
                                                       bool& changed) noexcept
        {
            ImGui::SeparatorText("センサーの入り切り");
            if (BeginFieldTable("##event-row"))
            {
                // 選べるのは選んだアクターが持つ名前だけ。打った名前の写しを持たない
                FieldRow("センサー");
                if (ImGui::BeginCombo("##sensor", sensor.sensor.c_str()))
                {
                    for (const HitSensor* choice : SensorsOf(selected, m_actorFile))
                    {
                        const std::string& name = choice->Name();
                        if (ImGui::Selectable(name.c_str(), name == sensor.sensor))
                        {
                            sensor.sensor = name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                DrawBoolComboRow("長さの間", "##valid", sensor.valid, "外す\0入れる\0", changed);
                DrawFrameSpanRows(sensor.start, sensor.length, changed);
                EndFieldTable();
            }
        }

        void ActionTimelinePanel::RenderSelectedRow(LevelEditorController& editor) noexcept
        {
            ActorActionInfo* action = WorkingAction();
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
                RenderSelectedSensor(editor.SelectedActor(), action->sensors[sensorRow], changed);
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
        }
    } // namespace Editor
} // namespace NS
