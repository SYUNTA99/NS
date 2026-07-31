#include "Game/Level/ScreenFadeComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"

#include <algorithm>

namespace NS::Game::Level
{
    ScreenFadeComponent::ScreenFadeComponent() noexcept
        // 応答 (+10) より先に進める。全黒に達した tick のうちに応答が動き、やり直しが必ず全黒の裏に隠れる
        : NS::Object::OverlayRendererComponent(NS::Object::TickPriority::LateUpdate + 5)
    {}

    void ScreenFadeComponent::OnUpdate()
    {
        Advance(NS::Core::FrameTimer::FixedDelta());
    }

    void ScreenFadeComponent::BeginOut(float seconds) noexcept
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

    void ScreenFadeComponent::BeginIn(float seconds) noexcept
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

    void ScreenFadeComponent::Cancel() noexcept
    {
        m_stage = Stage::None;
        m_timer = 0.0f;
        m_alpha = 0.0f;
    }

    void ScreenFadeComponent::Advance(float dt) noexcept
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

    void ScreenFadeComponent::OnRenderOverlay(const NS::Graphics::RenderContext& ctx)
    {
        if (m_alpha <= 0.0f)
            return;
        ctx.renderer->DrawFullscreenColor(NS::Math::Color{0.0f, 0.0f, 0.0f, m_alpha});
    }

} // namespace NS::Game::Level
