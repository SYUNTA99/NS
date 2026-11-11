#include "NSlib/CoreObject/System/MusicDirector.h"

#include "NSlib/CoreObject/Asset/Sound.h"

namespace NS
{
    bool MusicDirector::Start(Audio::SoundScene* sounds, const Sound* music, float volume)
    {
        if (m_started)
        {
            return false;
        }
        m_started = true;
        if (sounds == nullptr || music == nullptr)
        {
            return true;
        }
        m_handle = sounds->Play(music->XA2(), volume, true);
        return true;
    }

    void MusicDirector::Stop(Audio::SoundScene* sounds) noexcept
    {
        m_started = false;
        if (sounds != nullptr)
        {
            sounds->Stop(m_handle);
        }
        m_handle = {};
    }
} // namespace NS
