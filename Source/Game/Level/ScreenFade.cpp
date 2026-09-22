#include "Game/Level/ScreenFade.h"

#include "Runtime/Platform/Clock.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"

#include <algorithm>

namespace NS::Game::Level
{
    ScreenFade::ScreenFade() noexcept
        // 応答より先の帯。やり直しが必ず全黒の裏に隠れる
        : NS::Obj::OverlayRenderer(NS::Obj::TickPriority::LateUpdate + 5)
    {}

    void ScreenFade::OnUpdate()
    {
        Advance(NS::Platform::FrameTimer::FixedDelta());
    }

    void ScreenFade::BeginOut(float seconds) noexcept
    {
        if (m_stage != Stage::None)
            return;
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
            return;
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
            return;

        m_timer += dt;
        if (m_timer < m_duration)
            return;

        m_stage = (m_stage == Stage::Out) ? Stage::Hold : Stage::None;
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

    void ScreenFade::OnRenderOverlay(const NS::Gfx::RenderContext& ctx)
    {
        const float alpha = Alpha();
        if (alpha <= 0.0f)
            return;
        ctx.renderer->DrawFullscreenColor(NS::Core::Color{0.0f, 0.0f, 0.0f, alpha});
    }

} // namespace NS::Game::Level
