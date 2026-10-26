#include "NSlib/CoreObject/Action/ActionTimeline.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cstddef>
#include <variant>

namespace NS
{
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
        // 始まらなかった時は、前の動作の長さで今の状態を終わらせない
        m_endEntry = 0;
        m_endLength = 0;
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
        m_endEntry = machine->EntryCount();
        m_endLength = m_actions[found].length;
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
        const uint32 steps = machine->StepCount();
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
