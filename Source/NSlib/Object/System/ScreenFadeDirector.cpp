#include "NSlib/Object/System/ScreenFadeDirector.h"

#include "NSlib/Windows/Clock.h"

namespace NS::Obj
{
    void ScreenFadeDirector::OnTick()
    {
        m_fade.Advance(NS::OS::FrameTimer::FixedDelta());
    }
} // namespace NS::Obj
