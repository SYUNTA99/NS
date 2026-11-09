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
        // クリップの時刻をフレームの数へ直す時に、float の足し算の誤差で 1 フレーム前へ切り捨てないための余白
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
        return true;
    }

    void ActorAction::BindClip(const ActionClock& clock) noexcept
    {
        if (!IsRunning() || m_state != nullptr)
        {
            return;
        }
        m_clip = clock.clip;
        m_clipLoops = clock.clipLoops;
        m_clipBound = true;
        // 流れていたクリップがそのまま続く時は、今の時刻から数える。前のフレームの事象をまとめて起こさない
        const std::optional<int> now = ClockFrame(clock);
        if (now.has_value())
        {
            m_frame = std::max(m_frame, *now);
        }
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
        m_clip = nullptr;
        m_clipLoops = 0;
        m_clipBound = false;
        m_started.clear();
    }

    std::optional<int> ActorAction::ClockFrame(const ActionClock& clock) const noexcept
    {
        if (m_state != nullptr)
        {
            // 状態の歩 k の OnStep が走ったフレームに、フレーム k - 1 の事象を起こす
            return static_cast<int>(clock.machine->StepCount()) - 1;
        }
        // 結ぶ前はフレーム 0 だけ。結んだクリップが無いか長さが 0 なら進まない
        if (!m_clipBound)
        {
            return 0;
        }
        if (m_clip == nullptr || m_clip->duration <= 0.0f)
        {
            return std::nullopt;
        }
        // クリップの時刻を固定フレームの数に丸める。k 回足した秒は k
        // フレーム分をわずかに下回る事があるので、余白を足して切り捨てる
        return static_cast<int>(std::floor(clock.clipTime / OS::FrameTimer::FixedDelta() + k_FrameRoundingSlack));
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
        // 結んだクリップが別のクリップへ替わったら、状態に結んだ動作が状態を出た時と同じく止める
        if (m_state == nullptr && m_clipBound && m_clip != nullptr && clock.clip != m_clip)
        {
            Stop();
            return;
        }
        const std::optional<int> clockFrame = ClockFrame(clock);
        if (!clockFrame.has_value())
        {
            return;
        }
        // 同じクリップが頭から流れ直した (ループの回数が減った) 時は、フレーム 0 から起こし直す
        if (m_state == nullptr && clock.clipLoops < m_clipLoops)
        {
            m_clipLoops = clock.clipLoops;
            EndSensorSwitches(std::nullopt);
            m_frame = -1;
        }
        if (m_state == nullptr && clock.clipLoops > m_clipLoops)
        {
            // 前の周で届かなかったクリップの終わりまでと、1 回の Advance の間に過ぎた周の全部を起こしてから、
            // センサーを戻してフレーム 0 から起こし直す
            const int last = LastClipFrame();
            RunFrames(m_frame + 1, last);
            for (uint32 lap = m_clipLoops + 1; lap < clock.clipLoops; ++lap)
            {
                EndSensorSwitches(std::nullopt);
                RunFrames(0, last);
            }
            m_clipLoops = clock.clipLoops;
            EndSensorSwitches(std::nullopt);
            m_frame = -1;
        }
        // 時計の進まなかったフレームは、前と同じかそれより前のフレームになるので回さない
        if (*clockFrame <= m_frame)
        {
            return;
        }
        // 時計が 2 フレーム以上進んだ時 (アニメーションの再生が速い時) は、飛んだフレームの事象も起こす
        RunFrames(m_frame + 1, *clockFrame);
        m_frame = *clockFrame;
    }

    int ActorAction::LastClipFrame() const noexcept
    {
        // 時刻がクリップの長さより前に始まる最後のフレーム
        const float frames = m_clip->duration / OS::FrameTimer::FixedDelta();
        return static_cast<int>(std::ceil(frames - k_FrameRoundingSlack)) - 1;
    }

    void ActorAction::RunFrames(int from, int to)
    {
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
