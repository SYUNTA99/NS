#include "Game/Player/PlayerSounds.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"

namespace GL
{
    namespace Player
    {
        PlayerSounds::PlayerSounds()
        {
            NS::LoadAssetField(m_landSound, "Assets/Sounds/land.wav");
            NS::LoadAssetField(m_goalSound, "Assets/Sounds/goal.wav");
            NS::LoadAssetField(m_fallSound, "Assets/Sounds/fall.wav");
            NS::LoadAssetField(m_reboundSound, "Assets/Sounds/rebound.wav");
        }

        void PlayerSounds::AllowLand() noexcept
        {
            m_landAllowed = true;
        }

        void PlayerSounds::RequestLand() noexcept
        {
            // 接地が細かく切れて着地が続く時に、音を重ねて鳴らさない
            if (m_landAllowed && m_sinceLandSound >= m_landMinInterval)
            {
                m_landRequested = true;
            }
        }

        void PlayerSounds::Update(const NS::IGetAudio* audio, float dt)
        {
            m_sinceLandSound += dt;
            NS_ASSERT(Game, audio != nullptr, "PlayerSounds::Update: 音を鳴らすアクターが空");
            if (m_landRequested)
            {
                NS::PlaySound(audio, m_landSound, m_landVolume);
                m_sinceLandSound = 0.0f;
            }
            m_landRequested = false;
        }

        void PlayerSounds::PlayGoal(const NS::IGetAudio* audio) const
        {
            NS_ASSERT(Game, audio != nullptr, "PlayerSounds::PlayGoal: 音を鳴らすアクターが空");
            NS::PlaySound(audio, m_goalSound, m_goalVolume);
        }

        void PlayerSounds::PlayFall(const NS::IGetAudio* audio) const
        {
            NS_ASSERT(Game, audio != nullptr, "PlayerSounds::PlayFall: 音を鳴らすアクターが空");
            NS::PlaySound(audio, m_fallSound, m_fallVolume);
        }

        void PlayerSounds::PlayRebound(const NS::IGetAudio* audio) const
        {
            NS_ASSERT(Game, audio != nullptr, "PlayerSounds::PlayRebound: 音を鳴らすアクターが空");
            NS::PlaySound(audio, m_reboundSound, m_reboundVolume);
        }

        void PlayerSounds::Reset() noexcept
        {
            m_landRequested = false;
            m_landAllowed = false;
            m_sinceLandSound = std::numeric_limits<float>::infinity();
        }
    } // namespace Player
} // namespace GL
