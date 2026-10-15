#include "NSlib/Object/SubObjects/StateMachine.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

#include <utility>

namespace NS::Obj
{
    void StateMachine::Hold(std::unique_ptr<IStateMachine> machine) noexcept
    {
        NS_ASSERT(Scene, m_machine == nullptr, "StateMachine::Hold: 機械は 1 つの部品に 1 回だけ預ける");
        NS_ASSERT(Scene, machine != nullptr, "StateMachine::Hold: 空の機械を預けている");
        m_machine = std::move(machine);
    }

    void StateMachine::Update()
    {
        if (m_machine != nullptr)
        {
            m_machine->Step(BodyDelta());
        }
    }

    NS_CLASS(StateMachine)
} // namespace NS::Obj
