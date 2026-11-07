#include "NSlib/CoreObject/Action/ActorAction.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace NS
{
    namespace
    {
        // 流した秒をフレームの数へ直す時に、float の足し算の誤差で 1 フレーム前へ切り捨てないための余白
        // (フレームの割合)
        constexpr float k_FrameRoundingSlack = 1.0e-3f;
    } // namespace

    void ActorAction::ReloadActions(std::string_view actorFileName)
    {
        Stop();
        m_actions.clear();
        const nlohmann::json* actorFile = ActorFileLibrary::Get().Values(actorFileName);
        if (actorFile != nullptr)
        {
            m_actions = ReadActorActions(actorFileName, *actorFile);
        }
    }

    bool ActorAction::Begin(std::string_view name)
    {
        Stop();
        const std::size_t found = IndexOf(name);
        if (found == k_NoAction)
        {
            return false;
        }
        m_current = found;
        m_frame = -1;
        return true;
    }

    bool ActorAction::Start(std::string_view name)
    {
        if (!Begin(name))
        {
            return false;
        }
        m_state = nullptr;
        m_playedSeconds = 0.0f;
        return true;
    }

    bool ActorAction::StartWithState(std::string_view name, const IStateMachine& machine)
    {
        // 始まらなかった時は、前の動作の長さで今の状態を終わらせない
        m_endEntry = 0;
        m_endLength = 0;
        if (!Begin(name))
        {
            return false;
        }
        m_state = machine.CurrentId();
        m_endEntry = machine.EntryCount();
        m_endLength = m_actions[m_current].length;
        return true;
    }

    const ActorActionInfo* ActorAction::FindAction(std::string_view name) const noexcept
    {
        const std::size_t index = IndexOf(name);
        if (index == k_NoAction)
        {
            return nullptr;
        }
        return &m_actions[index];
    }

    std::size_t ActorAction::IndexOf(std::string_view name) const noexcept
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

    void ActorAction::Stop()
    {
        EndSensorSwitches(std::nullopt);
        m_current = k_NoAction;
        m_state = nullptr;
        m_started.clear();
    }

    std::optional<int> ActorAction::ClockFrame(const ActionClock& clock) const noexcept
    {
        if (m_state != nullptr)
        {
            // 状態の歩 k の OnStep が走ったフレームに、フレーム k - 1 の事象を起こす
            return static_cast<int>(clock.machine->StepCount()) - 1;
        }
        // 流した秒を固定フレームの数に丸める。k 回足した秒は k
        // フレーム分をわずかに下回る事があるので、余白を足して切り捨てる
        return static_cast<int>(std::floor(m_playedSeconds / OS::FrameTimer::FixedDelta() + k_FrameRoundingSlack));
    }

    void ActorAction::Advance(const ActionClock& clock)
    {
        m_started.clear();
        if (!IsRunning())
        {
            return;
        }
        if (m_state != nullptr && (clock.machine == nullptr || clock.machine->CurrentId() != m_state))
        {
            Stop();
            return;
        }
        if (m_state == nullptr)
        {
            m_playedSeconds += std::max(clock.playbackSeconds, 0.0f);
        }
        const std::optional<int> clockFrame = ClockFrame(clock);
        // 時計の進まなかったフレームは、前と同じかそれより前のフレームになるので回さない
        if (!clockFrame.has_value() || *clockFrame <= m_frame)
        {
            return;
        }
        // 時計が 2 フレーム以上進んだ時 (アニメーションの再生が速い時) は、飛んだフレームの事象も起こす
        const int from = m_frame + 1;
        const int to = *clockFrame;
        m_frame = to;
        const ActorActionInfo& action = m_actions[m_current];
        for (int frame = from; frame <= to; ++frame)
        {
            // 同じ歩に終わる物と始まる物があれば、始まる物を残す
            EndSensorSwitches(frame);
            for (std::size_t i = 0; i < action.sensors.size(); ++i)
            {
                const ActionSensorEvent& event = action.sensors[i];
                if (event.start != frame || event.length <= 0)
                {
                    continue;
                }
                m_sensorChanges.push_back(ActionSensorChange{.sensor = event.sensor, .valid = event.valid});
                m_switched.push_back(i);
            }
            for (std::size_t i = 0; i < action.events.size(); ++i)
            {
                if (action.events[i].start == frame)
                {
                    m_started.push_back(i);
                }
            }
        }
    }

    std::string_view ActorAction::CurrentName() const noexcept
    {
        if (!IsRunning())
        {
            return {};
        }
        return m_actions[m_current].name;
    }

    const ActorActionInfo* ActorAction::CurrentAction() const noexcept
    {
        if (!IsRunning())
        {
            return nullptr;
        }
        return &m_actions[m_current];
    }

    std::vector<ActionSensorChange> ActorAction::TakeSensorChanges()
    {
        return std::exchange(m_sensorChanges, {});
    }

    void ActorAction::EndSensorSwitches(std::optional<int> endingFrame)
    {
        if (!IsRunning())
        {
            m_switched.clear();
            return;
        }
        const ActorActionInfo& action = m_actions[m_current];
        std::erase_if(m_switched, [this, &action, endingFrame](std::size_t index) {
            const ActionSensorEvent& event = action.sensors[index];
            if (endingFrame.has_value() && event.start + event.length != *endingFrame)
            {
                return false;
            }
            m_sensorChanges.push_back(ActionSensorChange{.sensor = event.sensor, .valid = !event.valid});
            return true;
        });
    }

    bool IsStateEnd(const SceneActor* actor) noexcept
    {
        NS_ASSERT(Scene, actor != nullptr, "IsStateEnd: 持ち主が null");
        const ActorAction* action = actor->ActorActionClassObj();
        const IStateMachine* machine = actor->GetStateMachine();
        if (action == nullptr || machine == nullptr)
        {
            return false;
        }
        // 動作を始めた入りの間だけ、その動作の長さで答える。移る予約の間は、予約の先が動作を始めた状態と
        // 同じでも入った回数はまだ変わらないので、回数が予約の間に返す uint32 の最大で見分ける
        const uint32 steps = machine->StepCount();
        if (machine->EntryCount() != action->m_endEntry || steps == static_cast<uint32>(-1))
        {
            return false;
        }
        if (action->m_endLength <= 0)
        {
            return false;
        }
        return steps >= static_cast<uint32>(action->m_endLength);
    }
} // namespace NS
