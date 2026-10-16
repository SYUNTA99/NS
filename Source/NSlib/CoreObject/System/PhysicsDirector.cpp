#include "NSlib/CoreObject/System/PhysicsDirector.h"

#include "NSlib/Windows/Clock.h"

namespace NS
{
    void PhysicsDirector::OnTick()
    {
        m_physicsScene.Update(OS::FrameTimer::FixedDelta());
    }
} // namespace NS
