#include "NSlib/Audio/SoundLoop.h"

#include "NSlib/Audio/SoundClip.h"
#include "NSlib/Audio/detail/SoundVolume.h"

#include <Audio.h>

namespace NS
{
    namespace Audio
    {
        SoundLoop::SoundLoop(const SoundClip& clip)
        {
            if (!clip.IsValid())
            {
                return;
            }
            m_instance = clip.Effect()->CreateInstance();
        }

        SoundLoop::~SoundLoop() = default;

        void SoundLoop::Play()
        {
            if (m_playing)
            {
                return;
            }
            m_playing = true;
            if (m_instance)
            {
                m_instance->Play(true);
            }
        }

        void SoundLoop::Stop()
        {
            m_playing = false;
            if (m_instance)
            {
                m_instance->Stop();
            }
        }

        void SoundLoop::SetVolume(float volume)
        {
            if (m_instance)
            {
                m_instance->SetVolume(detail::ToXAudioVolume(volume));
            }
        }
    } // namespace Audio
} // namespace NS
