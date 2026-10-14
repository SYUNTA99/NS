#include "NSlib/Object/System/PhysicsDirector.h"

#include "NSlib/Windows/Clock.h"

namespace NS::Obj
{
    void PhysicsDirector::OnTick()
    {
        m_physicsScene.Update(NS::OS::FrameTimer::FixedDelta());
    }
} // namespace NS::Obj
