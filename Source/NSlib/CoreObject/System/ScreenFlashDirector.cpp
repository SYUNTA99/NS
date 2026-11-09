#include "NSlib/CoreObject/System/ScreenFlashDirector.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"

#include <algorithm>

namespace NS
{
    ScreenFlashDirector::ScreenFlashDirector(Scene& scene) : m_scene(scene)
    {
        InitOverlay(&m_scene, this);
    }

    ScreenFlashDirector::~ScreenFlashDirector() noexcept
    {
        EndOverlay(&m_scene, this);
    }

    void ScreenFlashDirector::Start(const SceneActor* requester, int frames, float alpha) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "ScreenFlashDirector::Start: 頼んだ物が null");
        m_requester = requester;
        m_remaining = std::max(frames, 0);
        m_frames = m_remaining;
        m_alpha = alpha;
        m_justStarted = true;
    }

    void ScreenFlashDirector::Stop(const SceneActor* requester) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "ScreenFlashDirector::Stop: 頼んだ物が null");
        if (m_requester == requester)
        {
            Clear();
        }
    }

    void ScreenFlashDirector::Scale(const SceneActor* requester, float scale) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "ScreenFlashDirector::Scale: 頼んだ物が null");
        if (m_requester == requester)
        {
            m_alpha *= scale;
        }
    }

    void ScreenFlashDirector::Clear() noexcept
    {
        m_remaining = 0;
        m_justStarted = false;
        m_requester = nullptr;
    }

    float ScreenFlashDirector::Alpha() const noexcept
    {
        if (m_remaining <= 0 || m_frames <= 0)
        {
            return 0.0f;
        }
        // 暗転の黒とは別の、瞬間に薄れる白。フレームごとに直線で下げる
        return m_alpha * (static_cast<float>(m_remaining) / static_cast<float>(m_frames));
    }

    void ScreenFlashDirector::OnTick() noexcept
    {
        if (m_justStarted)
        {
            m_justStarted = false;
        }
        else if (m_remaining > 0)
        {
            --m_remaining;
        }
    }

    void ScreenFlashDirector::OnRenderOverlay(const Gfx::RenderContext& context)
    {
        if (context.device == nullptr || context.overlay == nullptr)
        {
            return;
        }
        const float alpha = Alpha();
        if (alpha > 0.0f)
        {
            context.overlay->DrawFullscreenColor(Color{1.0f, 1.0f, 1.0f, alpha});
        }
    }
} // namespace NS
