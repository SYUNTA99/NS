#pragma once

#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    namespace Audio
    {
        class XA2Sound;
    } // namespace Audio

    //! @brief 道筋で選んだ 1 本の音
    //! @details AssetManager が装置を受け取っていない時と、読めない時は音が無く、道筋だけを持つ
    class Sound : public Object
    {
    public:
        //! xa2 が nullptr か無効なら音の無い Sound
        Sound(std::string path, std::unique_ptr<Audio::XA2Sound> xa2) noexcept;
        ~Sound() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;
        //! 鳴らせる音を持つか
        [[nodiscard]] bool IsValid() const noexcept;
        //! 読んだ音。音の無い Sound は nullptr
        [[nodiscard]] Audio::XA2Sound* XA2() const noexcept;

        NS_REFLECT_NONE(Sound, Object)

    private:
        std::string m_path;
        std::unique_ptr<Audio::XA2Sound> m_xa2;
    };
} // namespace NS
