#include "Game/Player/PlayerSounds.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"

namespace GL
{
    namespace Player
    {
        void PlayerSounds::Preload(const NS::IGetAudio* audio)
        {
            NS_ASSERT(Game, audio != nullptr, "PlayerSounds::Preload: 音を鳴らすアクターが空");
            NS::PreloadSounds(audio, {m_jumpSound, m_landSound, m_goalSound, m_fallSound});
        }

        void PlayerSounds::RequestJump() noexcept
        {
            m_jumpRequested = true;
            m_landAllowed = true;
        }

        void PlayerSounds::AllowLand() noexcept
        {
            m_landAllowed = true;
        }

        void PlayerSounds::RequestLand() noexcept
        {
            if (m_landAllowed)
            {
                m_landRequested = true;
            }
        }

        void PlayerSounds::Update(const NS::IGetAudio* audio)
        {
            NS_ASSERT(Game, audio != nullptr, "PlayerSounds::Update: 音を鳴らすアクターが空");
            if (m_jumpRequested)
            {
                NS::PlaySound(audio, m_jumpSound, m_jumpVolume);
            }
            if (m_landRequested)
            {
                NS::PlaySound(audio, m_landSound, m_landVolume);
            }
            m_jumpRequested = false;
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

        void PlayerSounds::Reset() noexcept
        {
            m_jumpRequested = false;
            m_landRequested = false;
            m_landAllowed = false;
        }
    } // namespace Player
} // namespace GL
