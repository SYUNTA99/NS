#include "NSlib/Gfx/ScreenFade.h"

#include "NSlib/Gfx/Renderer.h"

#include <algorithm>

namespace NS
{
    namespace Gfx
    {
        void ScreenFade::Render(Renderer& renderer) const noexcept
        {
            const float alpha = Alpha();
            if (alpha > 0.0f)
            {
                renderer.DrawFullscreenColor(Color{0.0f, 0.0f, 0.0f, alpha});
            }
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
                return;
            }
            m_stage = Stage::Out;
            m_duration = seconds;
            m_timer = 0.0f;
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
                return;
            }
            m_stage = Stage::In;
            m_duration = seconds;
            m_timer = 0.0f;
        }

        void ScreenFade::Cancel() noexcept
        {
            m_stage = Stage::None;
            m_timer = 0.0f;
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
    } // namespace Gfx
} // namespace NS
