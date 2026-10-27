#pragma once

// <xaudio2.h> を直に読まない。DirectXTK の Audio.h が XAUDIO2_HELPER_FUNCTIONS を定義してから読む
#include <Audio.h>

#include "NSlib/Core/Math.h"

#include <cmath>

namespace NS
{
    namespace Audio
    {
        namespace detail
        {
            //! XAudio2 へ渡せる音量にする。非数と負は 0、XAudio2 の上限を超えたら上限
            [[nodiscard]] inline float ToXAudioVolume(float volume) noexcept
            {
                if (std::isnan(volume))
                {
                    return 0.0f;
                }
                return Clamp(volume, 0.0f, XAUDIO2_MAX_VOLUME_LEVEL);
            }
        } // namespace detail
    } // namespace Audio
} // namespace NS
