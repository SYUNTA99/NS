#include "NSlib/CoreObject/Asset/Sound.h"

#include "NSlib/Audio/XA2Sound.h"

#include <utility>

namespace NS
{
    Sound::Sound(std::string path, std::unique_ptr<Audio::XA2Sound> xa2) noexcept : m_path(std::move(path))
    {
        if (xa2 && xa2->IsValid())
        {
            m_xa2 = std::move(xa2);
        }
    }

    Sound::~Sound() noexcept = default;

    const std::string& Sound::Path() const noexcept
    {
        return m_path;
    }

    bool Sound::IsValid() const noexcept
    {
        return m_xa2 != nullptr;
    }

    Audio::XA2Sound* Sound::XA2() const noexcept
    {
        return m_xa2.get();
    }
} // namespace NS
