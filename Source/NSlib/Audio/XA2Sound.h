#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <memory>
#include <string_view>

namespace DirectX
{
    class SoundEffect;
} // namespace DirectX

namespace NS
{
    namespace Audio
    {
        class AudioDevice;

        //! @brief 1 本の WAV の音を持つ。鳴らすのは SoundScene
        //! @details 読めるのは PCM・IEEE float・MS-ADPCM の WAV。MS-ADPCM は 1 ブロックが 32〜512 標本の物だけ
        //!          (XAudio2 の範囲)。ファイルが無い・WAV でない・形式の値が壊れている・範囲の外の MS-ADPCM の時は
        //!          ERROR を出して IsValid() が false になる。読めても装置が無効な時は、
        //!          ERROR を出さずに IsValid() が false になる (音を作らない設定は失敗でないため)
        class XA2Sound : private NonCopyable
        {
        public:
            //! @param device 音を作る装置。nullptr でもよく、その時は無効な装置と同じに扱う
            //! @param path WAV のファイルの道筋 (UTF-8)
            XA2Sound(AudioDevice* device, std::string_view path);
            ~XA2Sound();

            //! 鳴らせる音か
            [[nodiscard]] bool IsValid() const noexcept
            {
                return m_effect != nullptr;
            }

            //! SoundScene がソースボイスを作る時に使う。無効な音では nullptr
            [[nodiscard]] DirectX::SoundEffect* Effect() const noexcept
            {
                return m_effect.get();
            }

        private:
            std::unique_ptr<DirectX::SoundEffect> m_effect;
        };
    } // namespace Audio
} // namespace NS
