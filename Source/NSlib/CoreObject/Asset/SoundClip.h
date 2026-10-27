#pragma once

#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    namespace Audio
    {
        class SoundClip;
    } // namespace Audio

    //! @brief 道筋で選んだ 1 本の音
    //! @details AssetManager が装置を受け取っていない時と、読めない時は音が無く、道筋だけを持つ
    class SoundClip : public Object
    {
    public:
        //! clip が nullptr か無効なら音の無い SoundClip
        SoundClip(std::string path, std::unique_ptr<Audio::SoundClip> clip) noexcept;
        ~SoundClip() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;
        //! 鳴らせる音を持つか
        [[nodiscard]] bool IsValid() const noexcept;
        //! 読んだ音。音の無い SoundClip は nullptr
        [[nodiscard]] Audio::SoundClip* Clip() const noexcept;

        NS_REFLECT_NONE(SoundClip, Object)

    private:
        std::string m_path;
        std::unique_ptr<Audio::SoundClip> m_clip;
    };
} // namespace NS
