#include "NSlib/Object/ScreenFade.h"

#include "NSlib/Windows/Clock.h"
#include "NSlib/UI/ColorRect.h"

#include <algorithm>

namespace NS::Obj
{
    ScreenFade::ScreenFade() noexcept
    {
        NS::UI::ColorRect* black = Widgets().Root().AddChild<NS::UI::ColorRect>();
        black->SetStretch(true);
        black->SetColor(NS::Color{0.0f, 0.0f, 0.0f, 1.0f});
        SyncWidget();
    }

    void ScreenFade::SyncWidget() noexcept
    {
        Widgets().Root().SetAlpha(Alpha());
    }

    void ScreenFade::OnTick()
    {
        Advance(NS::OS::FrameTimer::FixedDelta());
    }

    void ScreenFade::BeginOut(float seconds) noexcept
    {
        if (m_stage != Stage::None)
        {
            return;
        }
        if (seconds <= 0.0f)
        {
            m_stage = Stage::Hold;
            SyncWidget();
            return;
        }
        m_stage = Stage::Out;
        m_duration = seconds;
        m_timer = 0.0f;
        SyncWidget();
    }

    void ScreenFade::BeginIn(float seconds) noexcept
    {
        if (m_stage != Stage::Hold)
        {
            return;
        }
        if (seconds <= 0.0f)
        {
            m_stage = Stage::None;
            SyncWidget();
            return;
        }
        m_stage = Stage::In;
        m_duration = seconds;
        m_timer = 0.0f;
        SyncWidget();
    }

    void ScreenFade::Cancel() noexcept
    {
        m_stage = Stage::None;
        m_timer = 0.0f;
        SyncWidget();
    }

    void ScreenFade::Advance(float dt) noexcept
    {
        if (!IsFading())
        {
            return;
        }
        m_timer += dt;
        if (m_timer < m_duration)
        {
            SyncWidget();
            return;
        }
        if (m_stage == Stage::Out)
        {
            m_stage = Stage::Hold;
        }
        else
        {
            m_stage = Stage::None;
        }
        SyncWidget();
    }

    float ScreenFade::Alpha() const noexcept
    {
        switch (m_stage)
        {
        case Stage::Hold:
            return 1.0f;
        case Stage::Out:
            return std::clamp(m_timer / m_duration, 0.0f, 1.0f);
        case Stage::In:
            return 1.0f - std::clamp(m_timer / m_duration, 0.0f, 1.0f);
        default:
            return 0.0f;
        }
    }
} // namespace NS::Obj
