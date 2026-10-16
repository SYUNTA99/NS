#include "NSlib/CoreObject/System/ScreenFadeDirector.h"

#include "NSlib/Windows/Clock.h"

namespace NS
{
    void ScreenFadeDirector::OnTick()
    {
        m_fade.Advance(OS::FrameTimer::FixedDelta());
    }
} // namespace NS
