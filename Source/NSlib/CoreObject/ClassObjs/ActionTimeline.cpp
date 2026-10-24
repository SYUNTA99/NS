#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <set>
#include <utility>
#include <variant>

namespace NS
{
    namespace
    {
        // 事象の行 1 つを読む。時計は状態に入ったフレームを 0 と数えるので、負の始まりは読まない
        [[nodiscard]] std::optional<ActorActionEvent> ParseEvent(const nlohmann::json& row,
                                                                 std::size_t index,
                                                                 std::string& error)
        {
            std::string reason;
            std::optional<HitReactionItem> item = ParseHitReactionItem(row, {"start", "length"}, reason);
            if (!item.has_value())
            {
                error = std::format("{} 番目の事象: {}", index, reason);
                return std::nullopt;
            }
            ActorActionEvent event{.item = std::move(*item)};
            if (!ReadHitReactionFrames(row, "start", event.start))
            {
                error = std::format("{} 番目の事象の始まり start が 0 以上の整数でない", index);
                return std::nullopt;
            }
            if (!ReadHitReactionFrames(row, "length", event.length))
            {
                error = std::format("{} 番目の事象の長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            return event;
        }

        // row の鍵 key の真偽を out へ読む。鍵が無ければ out は既定のまま true。真偽でなければ false
        [[nodiscard]] bool ReadOptionalBool(const nlohmann::json& row, std::string_view key, bool& out)
        {
            const nlohmann::json::const_iterator value = row.find(key);
            if (value == row.end())
            {
                return true;
            }
            if (!value->is_boolean())
            {
                return false;
            }
            out = value->get<bool>();
            return true;
        }

        // 入り切りの行 1 つを読む
        [[nodiscard]] std::optional<ActionSensorEvent> ParseSensorSwitch(const nlohmann::json& row,
                                                                         std::size_t index,
                                                                         std::string& error)
        {
            if (!row.is_object())
            {
                error = std::format("{} 番目の入り切りが object でない", index);
                return std::nullopt;
            }
            ActionSensorEvent event;
            if (!ReadHitReactionFrames(row, "start", event.start))
            {
                error = std::format("{} 番目の入り切りの始まり start が 0 以上の整数でない", index);
                return std::nullopt;
            }
            if (!ReadHitReactionFrames(row, "length", event.length))
            {
                error = std::format("{} 番目の入り切りの長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            if (!ReadOptionalBool(row, "attack", event.attack))
            {
                error = std::format("{} 番目の入り切りの attack が真偽でない", index);
                return std::nullopt;
            }
            if (!ReadOptionalBool(row, "valid", event.valid))
            {
                error = std::format("{} 番目の入り切りの valid が真偽でない", index);
                return std::nullopt;
            }
            for (nlohmann::json::const_iterator entry = row.begin(); entry != row.end(); ++entry)
            {
                const std::string& key = entry.key();
                if (key != "attack" && key != "valid" && key != "start" && key != "length")
                {
                    NS_LOG_WARN(Scene, "{} 番目の入り切りに読み手のいない欄 {} があり、読まない", index, key);
                }
            }
            return event;
        }
    } // namespace

    std::optional<ActorAction> ParseActorAction(const nlohmann::json& doc, std::string& error)
    {
        if (!doc.is_object())
        {
            error = "動作が object でない";
            return std::nullopt;
        }
        ActorAction action;
        const nlohmann::json::const_iterator name = doc.find("name");
        if (name == doc.end() || !name->is_string() || name->get_ref<const std::string&>().empty())
        {
            error = "名前 name が空でない文字列でない";
            return std::nullopt;
        }
        action.name = name->get<std::string>();
        const nlohmann::json::const_iterator events = doc.find("events");
        if (events == doc.end() || !events->is_array())
        {
            error = std::format("動作 {} の事象の並び events が配列でない", action.name);
            return std::nullopt;
        }
        const nlohmann::json::const_iterator sensors = doc.find("sensors");
        if (sensors != doc.end() && !sensors->is_array())
        {
            error = std::format("動作 {} の入り切りの並び sensors が配列でない", action.name);
            return std::nullopt;
        }
        for (nlohmann::json::const_iterator entry = doc.begin(); entry != doc.end(); ++entry)
        {
            if (entry.key() != "name" && entry.key() != "events" && entry.key() != "sensors")
            {
                NS_LOG_WARN(Scene, "動作 {} に読み手のいない欄 {} があり、読まない", action.name, entry.key());
            }
        }
        if (sensors != doc.end())
        {
            action.sensors.reserve(sensors->size());
            for (std::size_t i = 0; i < sensors->size(); ++i)
            {
                std::string reason;
                std::optional<ActionSensorEvent> sensor = ParseSensorSwitch((*sensors)[i], i, reason);
                if (!sensor.has_value())
                {
                    error = std::format("動作 {} の {}", action.name, reason);
                    return std::nullopt;
                }
                action.sensors.push_back(*sensor);
            }
        }
        action.events.reserve(events->size());
        for (std::size_t i = 0; i < events->size(); ++i)
        {
            std::string reason;
            std::optional<ActorActionEvent> event = ParseEvent((*events)[i], i, reason);
            // 1 つでも読めなければ動作ごと読まない。一部だけの動作は、置いた並びと違う演出になる
            if (!event.has_value())
            {
                error = std::format("動作 {} の {}", action.name, reason);
                return std::nullopt;
            }
            action.events.push_back(std::move(*event));
        }
        return action;
    }

    nlohmann::json ActorActionToJson(const ActorAction& action)
    {
        nlohmann::json events = nlohmann::json::array();
        for (const ActorActionEvent& event : action.events)
        {
            nlohmann::json row;
            WriteHitReactionItem(event.item, row);
            row["start"] = event.start;
            row["length"] = event.length;
            events.push_back(std::move(row));
        }
        nlohmann::json doc;
        doc["name"] = action.name;
        doc["events"] = std::move(events);
        // 入り切りの無い動作は鍵を書かない。入り切りを使わない .actor の形を変えない
        if (!action.sensors.empty())
        {
            nlohmann::json sensors = nlohmann::json::array();
            for (const ActionSensorEvent& sensor : action.sensors)
            {
                nlohmann::json row;
                row["attack"] = sensor.attack;
                row["valid"] = sensor.valid;
                row["start"] = sensor.start;
                row["length"] = sensor.length;
                sensors.push_back(std::move(row));
            }
            doc["sensors"] = std::move(sensors);
        }
        return doc;
    }

    namespace
    {
        // 動作の並びの要素 1 つを読んだ結果。読まない要素は action が空で、reason に理由を持つ
        struct ListedActorAction
        {
            std::optional<ActorAction> action;
            std::string reason;
        };

        // 動作の並びを要素ごとに読む。読むか読まないかの決まりは、読む時と書き戻す時でこの 1 か所を通す
        std::vector<ListedActorAction> ListActorActions(const nlohmann::json& list)
        {
            std::vector<ListedActorAction> listed(list.size());
            std::set<std::string, std::less<>> names;
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                std::optional<ActorAction> action = ParseActorAction(list[i], listed[i].reason);
                if (!action.has_value())
                {
                    continue;
                }
                // 同じ名前を 2 つ持つと、状態が始める動作が並びの順で黙って決まる
                if (!names.insert(action->name).second)
                {
                    listed[i].reason = std::format("名前 {} が前の動作と重なる", action->name);
                    continue;
                }
                listed[i].action = std::move(action);
            }
            return listed;
        }

        // 始まりと長さを足した終わりのフレーム。int を越える終わりは int の最大にする
        int EndFrame(int start, int length) noexcept
        {
            const int64 end = static_cast<int64>(start) + static_cast<int64>(length);
            return static_cast<int>(std::min<int64>(end, std::numeric_limits<int>::max()));
        }
    } // namespace

    std::vector<ActorAction> ReadActorActions(std::string_view actorFileName, const nlohmann::json& actorFile)
    {
        std::vector<ActorAction> actions;
        const nlohmann::json* list = ActorFileActions(actorFile);
        if (list == nullptr)
        {
            return actions;
        }
        if (!list->is_array())
        {
            NS_LOG_ERROR(Scene, ".actor {} の動作の並び actions が配列でないので、動作を読まない", actorFileName);
            return actions;
        }
        std::vector<ListedActorAction> listed = ListActorActions(*list);
        for (std::size_t i = 0; i < listed.size(); ++i)
        {
            if (!listed[i].action.has_value())
            {
                NS_LOG_ERROR(Scene, ".actor {} の {} 番目の動作を読まない。{}", actorFileName, i, listed[i].reason);
                continue;
            }
            actions.push_back(std::move(*listed[i].action));
        }
        return actions;
    }

    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorAction>& actions)
    {
        nlohmann::json list = nlohmann::json::array();
        std::size_t next = 0;
        const nlohmann::json* current = ActorFileActions(actorFile);
        if (current != nullptr && current->is_array())
        {
            // 読まなかった動作は字面のまま同じ位置に残す。捨てると、直していない本人の値が .actor から消える
            const std::vector<ListedActorAction> listed = ListActorActions(*current);
            for (std::size_t i = 0; i < listed.size(); ++i)
            {
                if (!listed[i].action.has_value())
                {
                    list.push_back((*current)[i]);
                    continue;
                }
                if (next < actions.size())
                {
                    list.push_back(ActorActionToJson(actions[next]));
                    ++next;
                }
            }
        }
        for (; next < actions.size(); ++next)
        {
            list.push_back(ActorActionToJson(actions[next]));
        }
        SetActorFileActions(actorFile, std::move(list));
    }

    int ActionLength(const ActorAction& action) noexcept
    {
        int length = 0;
        for (const ActorActionEvent& event : action.events)
        {
            length = std::max(length, EndFrame(event.start, event.length));
        }
        for (const ActionSensorEvent& event : action.sensors)
        {
            length = std::max(length, EndFrame(event.start, event.length));
        }
        return length;
    }

    void ActionTimeline::ReloadActions(const SceneActor* owner)
    {
        NS_ASSERT(Scene, owner != nullptr, "ActionTimeline::ReloadActions: 持ち主が null");
        Stop();
        m_actions.clear();
        const nlohmann::json* actorFile = ActorFileLibrary::Get().Values(owner->ActorFileName());
        if (actorFile != nullptr)
        {
            m_actions = ReadActorActions(owner->ActorFileName(), *actorFile);
        }
    }

    bool ActionTimeline::Start(const ActionTimelineSelf& owner,
                               std::string_view name,
                               int startFrame,
                               const Vector3& position,
                               const HitSensor* self,
                               const HitSensor* other)
    {
        NS_ASSERT(Scene, owner.actor != nullptr, "ActionTimeline::Start: 持ち主が null");
        NS_ASSERT(Scene, self != nullptr && other != nullptr, "ActionTimeline::Start: センサーが null");
        NS_ASSERT(Scene, startFrame >= 0, "ActionTimeline::Start: 始めるフレーム {} が負", startFrame);
        Stop();
        const IStateMachine* machine = owner.actor->GetStateMachine();
        NS_ASSERT(Scene, machine != nullptr, "ActionTimeline::Start: 状態機械の無い持ち主で動作を始めている");
        if (machine == nullptr)
        {
            return false;
        }
        const std::size_t found = IndexOf(name);
        if (found == k_NoAction)
        {
            NS_LOG_WARN(Scene, ".actor {} に動作 {} が無いので、始めなかった", owner.actor->ActorFileName(), name);
            return false;
        }
        // 項目は持ち主の HitReaction が始める。部品を作らないクラスの .actor に項目の事象を書いた誤り
        if (!m_actions[found].events.empty() && owner.reaction == nullptr)
        {
            NS_LOG_ERROR(Scene,
                         ".actor {} の動作 {} は項目の事象を持つが、{} は HitReaction を持たないので、始めなかった",
                         owner.actor->ActorFileName(),
                         name,
                         owner.actor->ClassName());
            return false;
        }
        m_current = found;
        m_state = machine->CurrentId();
        m_startFrame = std::max(startFrame, 0);
        m_frame = m_startFrame - 1;
        m_position = position;
        m_self = self;
        m_other = other;
        return true;
    }

    const ActorAction* ActionTimeline::FindAction(std::string_view name) const noexcept
    {
        const std::size_t index = IndexOf(name);
        if (index == k_NoAction)
        {
            return nullptr;
        }
        return &m_actions[index];
    }

    std::size_t ActionTimeline::IndexOf(std::string_view name) const noexcept
    {
        for (std::size_t i = 0; i < m_actions.size(); ++i)
        {
            if (m_actions[i].name == name)
            {
                return i;
            }
        }
        return k_NoAction;
    }

    void ActionTimeline::Stop() noexcept
    {
        EndSensorSwitches(std::nullopt);
        m_current = k_NoAction;
        m_state = nullptr;
        m_self = nullptr;
        m_other = nullptr;
    }

    // TODO: 時計は状態の歩だけ。最初に骨付きの動作を作る時に、アニメのフレームを時計にする道を足す
    void ActionTimeline::Advance(const ActionTimelineSelf& owner)
    {
        NS_ASSERT(Scene, owner.actor != nullptr, "ActionTimeline::Advance: 持ち主が null");
        m_started.clear();
        if (!IsRunning())
        {
            return;
        }
        const IStateMachine* machine = owner.actor->GetStateMachine();
        if (machine == nullptr || machine->CurrentId() != m_state)
        {
            Stop();
            return;
        }
        // 状態の歩 k の OnStep が走ったフレームに、始める歩 + k の事象を起こす。入っただけで歩を
        // 進めていないフレームと、歩の進まなかったフレームは、前と同じかそれより前のフレームになるので回さない
        const uint32 steps = machine->StepsInState();
        const int frame = m_startFrame + static_cast<int>(steps) - 1;
        if (frame <= m_frame)
        {
            return;
        }
        const bool firstFrame = m_frame < m_startFrame;
        m_frame = frame;
        const ActorAction& action = m_actions[m_current];

        // 同じ歩に終わる物と始まる物があれば、始まる物を残す
        EndSensorSwitches(frame);
        for (std::size_t i = 0; i < action.sensors.size(); ++i)
        {
            const ActionSensorEvent& event = action.sensors[i];
            if (event.start != frame || event.length <= 0)
            {
                continue;
            }
            HitSensor* sensor = owner.bodySensor;
            if (event.attack)
            {
                sensor = owner.attackSensor;
            }
            if (sensor == nullptr)
            {
                NS_LOG_WARN(Scene,
                            ".actor {} の動作 {} の {} 番目の入り切りは、{} が持たないセンサーを指すので、切り替えなかった",
                            owner.actor->ActorFileName(),
                            action.name,
                            i,
                            owner.actor->ClassName());
                continue;
            }
            if (event.valid)
            {
                sensor->Validate();
            }
            else
            {
                sensor->Invalidate();
            }
            m_switched.push_back(SensorSwitch{.index = i, .sensor = sensor});
        }

        for (std::size_t i = 0; i < action.events.size(); ++i)
        {
            const ActorActionEvent& event = action.events[i];
            // 途中のフレームから入った最初の歩は、そのフレームの途中にある形をそのフレームの姿から描く
            int shapeFrame = 0;
            if (firstFrame && event.start < frame && frame < event.start + event.length &&
                std::holds_alternative<HitShapeItem>(event.item))
            {
                shapeFrame = frame - event.start;
            }
            else if (event.start != frame)
            {
                continue;
            }
            NS_ASSERT(Scene,
                      owner.reaction != nullptr,
                      "ActionTimeline::Advance: 項目の事象を持つ動作を、HitReaction を渡さずに進めている");
            owner.reaction->StartItem(HitReactionSelf{.actor = owner.actor, .model = owner.model},
                                      event.item,
                                      event.length,
                                      m_position,
                                      m_self,
                                      m_other,
                                      shapeFrame);
            m_started.push_back(i);
        }
    }

    std::string_view ActionTimeline::CurrentName() const noexcept
    {
        if (!IsRunning())
        {
            return {};
        }
        return m_actions[m_current].name;
    }

    void ActionTimeline::EndSensorSwitches(std::optional<int> endingFrame) noexcept
    {
        if (!IsRunning())
        {
            m_switched.clear();
            return;
        }
        const ActorAction& action = m_actions[m_current];
        std::erase_if(m_switched, [&action, endingFrame](const SensorSwitch& switched) {
            const ActionSensorEvent& event = action.sensors[switched.index];
            if (endingFrame.has_value() && event.start + event.length != *endingFrame)
            {
                return false;
            }
            if (event.valid)
            {
                switched.sensor->Invalidate();
            }
            else
            {
                switched.sensor->Validate();
            }
            return true;
        });
    }
} // namespace NS
