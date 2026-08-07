#include "Runtime/Graphics/ScreenFade.h"

#include "Runtime/Graphics/Renderer.h"

#include <algorithm>

namespace NS::Graphics
{
    void ScreenFade::BeginOut(float seconds) noexcept
    {
        if (m_stage != Stage::None)
            return;
        if (seconds <= 0.0f)
        {
            m_stage = Stage::Hold;
            m_alpha = 1.0f;
            return;
        }
        m_stage = Stage::Out;
        m_duration = seconds;
        m_timer = 0.0f;
        m_alpha = 0.0f;
    }

    void ScreenFade::BeginIn(float seconds) noexcept
    {
        if (m_stage != Stage::Hold)
            return;
        if (seconds <= 0.0f)
        {
            m_stage = Stage::None;
            m_alpha = 0.0f;
            return;
        }
        m_stage = Stage::In;
        m_duration = seconds;
        m_timer = 0.0f;
        m_alpha = 1.0f;
    }

    void ScreenFade::Cancel() noexcept
    {
        m_stage = Stage::None;
        m_timer = 0.0f;
        m_alpha = 0.0f;
    }

    void ScreenFade::Advance(float dt) noexcept
    {
        if (!IsFading())
            return;

        m_timer += dt;
        if (m_stage == Stage::Out)
        {
            m_alpha = std::clamp(m_timer / m_duration, 0.0f, 1.0f);
            if (m_timer >= m_duration)
                m_stage = Stage::Hold;
        }
        else
        {
            m_alpha = 1.0f - std::clamp(m_timer / m_duration, 0.0f, 1.0f);
            if (m_timer >= m_duration)
                m_stage = Stage::None;
        }
    }

    void ScreenFade::Render(Renderer& renderer) noexcept
    {
        if (m_alpha <= 0.0f)
            return;
        renderer.DrawFullscreenColor(NS::Core::Color{0.0f, 0.0f, 0.0f, m_alpha});
    }

} // namespace NS::Graphics
