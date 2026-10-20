#include "NSlib/CoreObject/ClassObjs/StateMachine.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Clock.h"

#include <utility>

namespace NS
{
    void StateMachine::Hold(std::unique_ptr<IStateMachine> machine) noexcept
    {
        NS_ASSERT(Scene, m_machine == nullptr, "StateMachine::Hold: 機械は 1 つの部品に 1 回だけ預ける");
        NS_ASSERT(Scene, machine != nullptr, "StateMachine::Hold: 空の機械を預けている");
        m_machine = std::move(machine);
    }

    void StateMachine::Update()
    {
        if (m_machine == nullptr)
        {
            return;
        }
        // 持ち主の無い部品は固定の 1 歩の秒で進む
        float dt = OS::FrameTimer::FixedDelta();
        if (m_owner != nullptr)
        {
            dt = GetFrameDelta(m_owner);
        }
        m_machine->Step(dt);
    }
} // namespace NS
