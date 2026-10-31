#include "NSlib/CoreObject/System/HitScreenDirector.h"

#include "NSlib/CoreObject/IGet/IGetRenderer.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/UpdatePhase.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"
#include "NSlib/Gfx/Render/RenderContext.h"

#include <algorithm>

namespace NS
{
    HitScreenDirector::HitScreenDirector(Scene& scene) : m_scene(scene)
    {
        m_scene.AddSystemTick(*this, UpdatePhase::RenderPrep);
        InitOverlay(&m_scene, this);
    }

    HitScreenDirector::~HitScreenDirector() noexcept
    {
        EndOverlay(&m_scene, this);
    }

    void HitScreenDirector::StartFlash(const SceneActor* requester, int frames, float alpha) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::StartFlash: 頼んだ物が null");
        m_flashRequester = requester;
        m_flashRemaining = std::max(frames, 0);
        m_flashFrames = m_flashRemaining;
        m_flashAlpha = alpha;
        m_flashJustStarted = true;
    }

    void HitScreenDirector::Stop(const SceneActor* requester) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::Stop: 頼んだ物が null");
        if (m_flashRequester == requester)
        {
            m_flashRemaining = 0;
            m_flashJustStarted = false;
            m_flashRequester = nullptr;
        }
    }

    void HitScreenDirector::Scale(const SceneActor* requester, float scale) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::Scale: 頼んだ物が null");
        if (m_flashRequester == requester)
        {
            m_flashAlpha *= scale;
        }
        if (m_ringActive && m_ringRequester == requester)
        {
            m_ring.push = m_ring.push.Scaled(scale);
            WriteDistortionRing();
        }
        if (m_waveActive && m_waveRequester == requester)
        {
            m_wave.strength = m_wave.strength.Scaled(scale);
            WriteGroundWave();
        }
    }

    void HitScreenDirector::StartDistortionRing(const SceneActor* requester, const HitDistortionRingDesc& desc) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::StartDistortionRing: 頼んだ物が null");
        EndDistortionRing();
        if (desc.frames <= 0)
        {
            return;
        }
        m_ringRequester = requester;
        m_ring = desc;
        m_ringElapsed = 0;
        m_ringActive = true;
        m_ringJustStarted = true;
        WriteDistortionRing();
    }

    void HitScreenDirector::StopDistortionRing(const SceneActor* requester) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::StopDistortionRing: 頼んだ物が null");
        if (m_ringRequester == requester)
        {
            EndDistortionRing();
        }
    }

    void HitScreenDirector::WriteDistortionRing() noexcept
    {
        const float frame = static_cast<float>(m_ringElapsed);
        m_scene.SetDistortionRing(Gfx::DistortionRing{.center = m_ring.center,
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
        m_scene.SetDistortionRing(Gfx::DistortionRing{});
    }

    void HitScreenDirector::StartGroundWave(const SceneActor* requester, const HitGroundWaveDesc& desc) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::StartGroundWave: 頼んだ物が null");
        EndGroundWave();
        if (desc.frames <= 0)
        {
            return;
        }
        m_waveRequester = requester;
        m_wave = desc;
        m_waveElapsed = 0;
        m_waveActive = true;
        m_waveJustStarted = true;
        WriteGroundWave();
    }

    void HitScreenDirector::StopGroundWave(const SceneActor* requester) noexcept
    {
        NS_ASSERT(Scene, requester != nullptr, "HitScreenDirector::StopGroundWave: 頼んだ物が null");
        if (m_waveRequester == requester)
        {
            EndGroundWave();
        }
    }

    void HitScreenDirector::WriteGroundWave() noexcept
    {
        const float frame = static_cast<float>(m_waveElapsed);
        m_scene.SetGroundWave(Gfx::GroundWave{.centerX = m_wave.center.x,
                                              .centerZ = m_wave.center.z,
                                              .radius = m_wave.radius.Evaluate(frame),
                                              .strength = m_wave.strength.Evaluate(frame)});
    }

    void HitScreenDirector::EndGroundWave() noexcept
    {
        // 波を出していない時は、場面の波に触らない
        if (!m_waveActive)
        {
            return;
        }
        m_waveActive = false;
        m_waveJustStarted = false;
        m_waveRequester = nullptr;
        m_scene.SetGroundWave(Gfx::GroundWave{});
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
        if (m_waveJustStarted)
        {
            m_waveJustStarted = false;
        }
        else if (m_waveActive)
        {
            ++m_waveElapsed;
            if (m_waveElapsed >= m_wave.frames)
            {
                EndGroundWave();
            }
            else
            {
                WriteGroundWave();
            }
        }
    }

    void HitScreenDirector::OnRenderOverlay(const Gfx::RenderContext& context)
    {
        if (context.device == nullptr || context.overlay == nullptr)
        {
            return;
        }
        const float alpha = FlashAlpha();
        if (alpha > 0.0f)
        {
            context.overlay->DrawFullscreenColor(Color{1.0f, 1.0f, 1.0f, alpha});
        }
    }
} // namespace NS
