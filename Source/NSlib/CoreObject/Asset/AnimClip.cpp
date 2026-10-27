#include "NSlib/CoreObject/Asset/AnimClip.h"

#include <utility>

namespace NS
{
    AnimClip::AnimClip(std::string path, std::unique_ptr<Anim::AnimClip> clip) noexcept
        : m_path(std::move(path)), m_clip(std::move(clip))
    {
    }

    AnimClip::~AnimClip() noexcept = default;

    const std::string& AnimClip::Path() const noexcept { return m_path; }

    const Anim::AnimClip* AnimClip::Clip() const noexcept { return m_clip.get(); }
} // namespace NS
