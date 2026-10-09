#include "NSlib/Object/Scene/HitScreenDirector.h"

#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/UpdatePhase.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace NS::Obj
{
    namespace
    {
        // 震えの線の画素の欄は描画先の高さがこの値のときの大きさで書く
        constexpr float k_ReferenceHeight = 720.0f;
        // 線の暗い縁が白い線からはみ出す片側の幅。描画先の高さ 720 のときの画素。明るい床の上でも白い線を読ませる
        constexpr float k_LineOutlineWidth = 1.5f;
    } // namespace

    std::array<HitShakeLineRect, 6> ShakeLineRects(const HitShakeLinesDesc& desc,
                                                   int frame,
                                                   const HitShakeLineSpan& span,
                                                   float pixelScale) noexcept
    {
        // 輪郭の外へ縦の線を並べる。外の線ほど短くし、輪郭から離れる向きを読ませる
        const int flip = std::max(desc.flipFrames, 1);
        const bool outward = (std::max(frame, 0) / flip) % 2 == 1;
        const float gap = desc.gapPixels * pixelScale;
        const float width = desc.widthPixels * pixelScale;
        float shift = 0.0f;
        if (outward)
        {
            shift = gap * 0.5f;
        }
        std::array<HitShakeLineRect, 6> rects{};
        for (int i = 0; i < 3; ++i)
        {
            const float fromOutline = gap + shift + static_cast<float>(i) * (width + gap * 0.6f);
            const float length = desc.lengthPixels * pixelScale * (1.0f - 0.25f * static_cast<float>(i));
            const float top = span.centerY - length * 0.5f;
            rects[static_cast<std::size_t>(i)] = HitShakeLineRect{span.left - fromOutline - width, top, width, length};
            rects[static_cast<std::size_t>(i + 3)] = HitShakeLineRect{span.right + fromOutline, top, width, length};
        }
        return rects;
    }

    HitScreenDirector::HitScreenDirector(Scene& scene) : m_scene(scene)
    {
        m_scene.Objects().AddTicker(this, UpdatePhase::RenderPrep);
        m_scene.RegisterOverlay(this);
    }

    HitScreenDirector::~HitScreenDirector() noexcept
    {
        m_scene.UnregisterOverlay(this);
        m_scene.Objects().RemoveTicker(this);
    }

    void HitScreenDirector::StartFlash(const Actor& requester, int frames, float alpha) noexcept
    {
        m_flashRequester = &requester;
        m_flashRemaining = std::max(frames, 0);
        m_flashFrames = m_flashRemaining;
        m_flashAlpha = alpha;
        m_flashJustStarted = true;
    }

    void HitScreenDirector::StartShakeLines(const Actor& requester, const HitShakeLinesDesc& desc) noexcept
    {
        if (!(desc.radius > 0.0f) || desc.frames <= 0)
        {
            m_linesRemaining = 0;
            m_linesJustStarted = false;
            return;
        }
        m_linesRequester = &requester;
        m_lines = desc;
        m_linesRemaining = desc.frames;
        m_linesJustStarted = true;
    }

    void HitScreenDirector::Stop(const Actor& requester) noexcept
    {
        if (m_flashRequester == &requester)
        {
            m_flashRemaining = 0;
            m_flashJustStarted = false;
            m_flashRequester = nullptr;
        }
        if (m_linesRequester == &requester)
        {
            m_linesRemaining = 0;
            m_linesJustStarted = false;
            m_linesRequester = nullptr;
        }
    }

    void HitScreenDirector::StartDistortionRing(const Actor& requester, const HitDistortionRingDesc& desc) noexcept
    {
        EndDistortionRing();
        if (desc.frames <= 0)
        {
            return;
        }
        m_ringRequester = &requester;
        m_ring = desc;
        m_ringElapsed = 0;
        m_ringActive = true;
        m_ringJustStarted = true;
        WriteDistortionRing();
    }

    void HitScreenDirector::StopDistortionRing(const Actor& requester) noexcept
    {
        if (m_ringRequester == &requester)
        {
            EndDistortionRing();
        }
    }

    void HitScreenDirector::WriteDistortionRing() noexcept
    {
        const float frame = static_cast<float>(m_ringElapsed);
        m_scene.SetDistortionRing(NS::Gfx::DistortionRing{.center = m_ring.center,
                                                          .radius = m_ring.radius.Evaluate(frame),
                                                          .push = m_ring.push.Evaluate(frame),
                                                          .halfWidth = m_ring.halfWidth});
    }

    void HitScreenDirector::EndDistortionRing() noexcept
    {
        // 輪を出していない時は、場面の輪に触らない
        if (!m_ringActive)
        {
            return;
        }
        m_ringActive = false;
        m_ringJustStarted = false;
        m_ringRequester = nullptr;
        m_scene.SetDistortionRing(NS::Gfx::DistortionRing{});
    }

    float HitScreenDirector::FlashAlpha() const noexcept
    {
        if (m_flashRemaining <= 0 || m_flashFrames <= 0)
        {
            return 0.0f;
        }
        // 暗転の黒とは別の、瞬間に薄れる白。フレームごとに直線で下げる
        return m_flashAlpha * (static_cast<float>(m_flashRemaining) / static_cast<float>(m_flashFrames));
    }

    void HitScreenDirector::OnTick()
    {
        if (m_flashJustStarted)
        {
            m_flashJustStarted = false;
        }
        else if (m_flashRemaining > 0)
        {
            --m_flashRemaining;
        }
        if (m_linesJustStarted)
        {
            m_linesJustStarted = false;
        }
        else if (m_linesRemaining > 0)
        {
            --m_linesRemaining;
        }
        if (m_ringJustStarted)
        {
            m_ringJustStarted = false;
        }
        else if (m_ringActive)
        {
            ++m_ringElapsed;
            if (m_ringElapsed >= m_ring.frames)
            {
                EndDistortionRing();
            }
            else
            {
                WriteDistortionRing();
            }
        }
    }

    void HitScreenDirector::OnRenderOverlay(const NS::Gfx::RenderContext& context)
    {
        if (context.renderer == nullptr)
        {
            return;
        }
        const float alpha = FlashAlpha();
        if (alpha > 0.0f)
        {
            context.renderer->DrawFullscreenColor(NS::Color{1.0f, 1.0f, 1.0f, alpha});
        }
        RenderShakeLines(context);
    }

    void HitScreenDirector::RenderShakeLines(const NS::Gfx::RenderContext& context) const noexcept
    {
        if (m_linesRemaining <= 0)
        {
            return;
        }
        const NS::Size2D size = context.renderer->Size();
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }
        const float width = static_cast<float>(size.width);
        const float height = static_cast<float>(size.height);
        // 真後ろのカメラでは自機が相手に重なるので、2 つを囲む幅の外へ出す。縦は 2 つの上端と下端の真ん中
        float left = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        float top = std::numeric_limits<float>::max();
        float bottom = std::numeric_limits<float>::lowest();
        const std::array<std::pair<NS::Vector3, float>, 2> bodies{std::pair{m_lines.center, m_lines.radius},
                                                                  std::pair{m_lines.otherCenter, m_lines.otherRadius}};
        for (const std::pair<NS::Vector3, float>& body : bodies)
        {
            if (!(body.second > 0.0f))
            {
                continue;
            }
            NS::Vector2 centerPixel{};
            float centerW = 0.0f;
            if (!NS::Gfx::TryProjectToPixels(context.viewProjection, body.first, width, height, centerPixel, centerW))
            {
                return;
            }
            const float radiusPixels =
                NS::Gfx::ProjectedLengthPixels(context.viewProjection, body.second, centerW, height);
            left = std::min(left, centerPixel.x - radiusPixels);
            right = std::max(right, centerPixel.x + radiusPixels);
            top = std::min(top, centerPixel.y - radiusPixels);
            bottom = std::max(bottom, centerPixel.y + radiusPixels);
        }
        const float pixelScale = height / k_ReferenceHeight;
        const HitShakeLineSpan span{.left = left, .right = right, .centerY = (top + bottom) * 0.5f};
        const std::array<HitShakeLineRect, 6> rects =
            ShakeLineRects(m_lines, m_lines.frames - m_linesRemaining, span, pixelScale);
        const float outline = k_LineOutlineWidth * pixelScale;
        for (const HitShakeLineRect& rect : rects)
        {
            context.renderer->DrawScreenRect(rect.x - outline,
                                             rect.y - outline,
                                             rect.width + outline * 2.0f,
                                             rect.height + outline * 2.0f,
                                             NS::Color{0.0f, 0.0f, 0.0f, 0.6f});
        }
        for (const HitShakeLineRect& rect : rects)
        {
            context.renderer->DrawScreenRect(
                rect.x, rect.y, rect.width, rect.height, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
} // namespace NS::Obj
