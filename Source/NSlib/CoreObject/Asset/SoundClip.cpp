#include "NSlib/CoreObject/Asset/SoundClip.h"

#include "NSlib/Audio/SoundClip.h"

#include <utility>

namespace NS
{
    SoundClip::SoundClip(std::string path, std::unique_ptr<Audio::SoundClip> clip) noexcept : m_path(std::move(path))
    {
        if (clip && clip->IsValid())
        {
            m_clip = std::move(clip);
        }
    }

    SoundClip::~SoundClip() noexcept = default;

    const std::string& SoundClip::Path() const noexcept
    {
        return m_path;
    }

    bool SoundClip::IsValid() const noexcept
    {
        return m_clip != nullptr;
    }

    Audio::SoundClip* SoundClip::Clip() const noexcept
    {
        return m_clip.get();
    }
} // namespace NS
