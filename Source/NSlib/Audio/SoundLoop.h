#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <memory>

namespace DirectX
{
    class SoundEffectInstance;
} // namespace DirectX

namespace NS
{
    namespace Audio
    {
        class SoundClip;

        //! @brief SoundClip を繰り返し鳴らす。曲のように、止めるまで鳴り続ける音に使う
        //! @details 無効な SoundClip から作った物は IsValid() が false で音を出さない。
        //!          IsPlaying() は装置の有無によらず、Play() から Stop() までの間 true を返す
        class SoundLoop : private NonCopyable
        {
        public:
            //! clip は、このループより長く生きている事
            explicit SoundLoop(const SoundClip& clip);
            ~SoundLoop();

            //! 音を出せるループか
            [[nodiscard]] bool IsValid() const noexcept
            {
                return m_instance != nullptr;
            }

            //! 鳴らし始める。鳴っている間に呼んでも始め直さない。機器を繋ぎ直して止まった後に呼ぶと、また鳴らす
            void Play();

            //! 止める。次の Play() は最初から鳴らす
            void Stop();

            //! 音量を変える。非数か負なら 0 にする
            void SetVolume(float volume);

            //! Play() から Stop() までの間か
            [[nodiscard]] bool IsPlaying() const noexcept
            {
                return m_playing;
            }

        private:
            std::unique_ptr<DirectX::SoundEffectInstance> m_instance;
            bool m_playing = false;
        };
    } // namespace Audio
} // namespace NS
