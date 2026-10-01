#include "Runtime/Object/Components/StateMachineComponent.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Clock.h"

namespace NS::Obj
{
    void StateMachineComponent::OnUpdate()
    {
        Actor* owner = Owner();
        if (owner != nullptr && m_machine.IsBuilt())
        {
            m_machine.Step(*owner, NS::Platform::FrameTimer::FixedDelta());
        }
    }

    NS_CLASS(StateMachineComponent)
} // namespace NS::Obj
