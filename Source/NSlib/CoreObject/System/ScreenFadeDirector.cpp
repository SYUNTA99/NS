#include "NSlib/CoreObject/System/ScreenFadeDirector.h"

#include "NSlib/Core/Windows/Clock.h"

namespace NS
{
    void ScreenFadeDirector::OnTick()
    {
        m_fade.Advance(OS::FrameTimer::FixedDelta());
    }
} // namespace NS
