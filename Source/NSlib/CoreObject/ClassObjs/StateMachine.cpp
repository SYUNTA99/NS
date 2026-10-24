#include "NSlib/CoreObject/ClassObjs/StateMachine.h"

#include "NSlib/Core/Assert.h"

#include <utility>

namespace NS
{
    void StateMachine::Hold(std::unique_ptr<IStateMachine> machine) noexcept
    {
        NS_ASSERT(Scene, m_machine == nullptr, "StateMachine::Hold: 機械は 1 つの部品に 1 回だけ預ける");
        NS_ASSERT(Scene, machine != nullptr, "StateMachine::Hold: 空の機械を預けている");
        m_machine = std::move(machine);
    }

    void StateMachine::Update(float dt)
    {
        if (m_machine == nullptr)
        {
            return;
        }
        m_machine->Step(dt);
    }
} // namespace NS
