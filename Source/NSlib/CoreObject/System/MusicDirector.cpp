#include "NSlib/CoreObject/System/MusicDirector.h"

#include "NSlib/Audio/SoundLoop.h"
#include "NSlib/CoreObject/Asset/Sound.h"

namespace NS
{
    MusicDirector::MusicDirector() noexcept = default;

    MusicDirector::~MusicDirector() noexcept = default;

    bool MusicDirector::Start(const Audio::AudioDevice* device, const Sound* music, float volume)
    {
        if (m_started)
        {
            return false;
        }
        m_started = true;
        if (device == nullptr || music == nullptr || !music->IsValid())
        {
            return true;
        }
        m_loop = std::make_unique<Audio::SoundLoop>(*music->XA2());
        m_loop->SetVolume(volume);
        m_loop->Play();
        return true;
    }

    void MusicDirector::Stop() noexcept
    {
        m_started = false;
        m_loop.reset();
    }

    void MusicDirector::Update()
    {
        if (m_loop)
        {
            m_loop->Play();
        }
    }
} // namespace NS
