#include "Game/Level/HitScreenDirector.h"

#include "NSlib/CoreObject/System/SceneRenderer.h"

namespace GL
{
    namespace Level
    {
        namespace
        {
            // 床の波はシーンの値の 1 つ目に、中心の X・中心の Z・半径・強さの順で並べる。読むのは Shaders/Game/SurfaceHooks.hlsli
            // 並びは書く関数と読む関数の 2 つだけが知る
            void WriteGroundWaveValue(NS::SceneRenderer& renderer, const GroundWaveFrame& wave) noexcept
            {
                renderer.SetSceneValue(0, NS::Vector4{wave.centerX, wave.centerZ, wave.radius, wave.strength});
            }

            [[nodiscard]] GroundWaveFrame ReadGroundWaveValue(const NS::SceneRenderer& renderer) noexcept
            {
                const NS::Vector4& value = renderer.SceneValuesShown()[0];
                return GroundWaveFrame{.centerX = value.x, .centerZ = value.y, .radius = value.z, .strength = value.w};
            }
        } // namespace

        GroundWaveFrame HitScreenDirector::GroundWaveShown(const NS::SceneRenderer& renderer) noexcept
        {
            return ReadGroundWaveValue(renderer);
        }

        void HitScreenDirector::StartDistortionRing(const NS::SceneActor* requester,
                                                    const HitDistortionRingDesc& desc) noexcept
        {
            NS_ASSERT(Game, requester != nullptr, "HitScreenDirector::StartDistortionRing: 頼んだ物が null");
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

        void HitScreenDirector::StopDistortionRing(const NS::SceneActor* requester) noexcept
        {
            NS_ASSERT(Game, requester != nullptr, "HitScreenDirector::StopDistortionRing: 頼んだ物が null");
            if (m_ringRequester == requester)
            {
                EndDistortionRing();
            }
        }

        void HitScreenDirector::WriteDistortionRing() noexcept
        {
            const float frame = static_cast<float>(m_ringElapsed);
            m_renderer.SetDistortionRing(NS::Gfx::DistortionRing{.center = m_ring.center,
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
            m_renderer.SetDistortionRing(NS::Gfx::DistortionRing{});
        }

        void HitScreenDirector::StartGroundWave(const NS::SceneActor* requester, const HitGroundWaveDesc& desc) noexcept
        {
            NS_ASSERT(Game, requester != nullptr, "HitScreenDirector::StartGroundWave: 頼んだ物が null");
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

        void HitScreenDirector::StopGroundWave(const NS::SceneActor* requester) noexcept
        {
            NS_ASSERT(Game, requester != nullptr, "HitScreenDirector::StopGroundWave: 頼んだ物が null");
            if (m_waveRequester == requester)
            {
                EndGroundWave();
            }
        }

        void HitScreenDirector::Scale(const NS::SceneActor* requester, float scale) noexcept
        {
            NS_ASSERT(Game, requester != nullptr, "HitScreenDirector::Scale: 頼んだ物が null");
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

        void HitScreenDirector::WriteGroundWave() noexcept
        {
            const float frame = static_cast<float>(m_waveElapsed);
            WriteGroundWaveValue(m_renderer,
                                 GroundWaveFrame{.centerX = m_wave.center.x,
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
            WriteGroundWaveValue(m_renderer, GroundWaveFrame{});
        }

        void HitScreenDirector::Clear() noexcept
        {
            EndDistortionRing();
            EndGroundWave();
        }

        void HitScreenDirector::OnTick() noexcept
        {
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
    } // namespace Level
} // namespace GL
