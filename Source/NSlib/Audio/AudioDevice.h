#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <memory>

namespace DirectX
{
    class AudioEngine;
} // namespace DirectX

namespace NS
{
    namespace Audio
    {
        //! @brief 音の機器との繋がりを持つ。音を鳴らす型はこの装置から作る
        //! @details 機器が無い時と使用中の時は、音無しで動いて IsValid() が true のままになる。
        //!          機器が抜けたら Update() がすぐ既定の機器へ繋ぎ直し、音無しの間は間をあけて繋ぎ直しを試す。
        //!          XAudio2 を作れない時は ERROR を出し、IsValid() が false になる
        class AudioDevice : private NonCopyable
        {
        public:
            //! enable が false の時は機器に繋がず、IsValid() が false になる (テストと Replay 用)
            explicit AudioDevice(bool enable);
            ~AudioDevice();

            //! 音を作れる状態か
            [[nodiscard]] bool IsValid() const noexcept
            {
                return m_engine != nullptr;
            }

            //! 毎フレーム呼ぶ。機器を失った時と音無しの間は既定の機器へ繋ぎ直す。無効な装置では何もしない
            void Update();

            //! 音の処理を止める。鳴っている途中の音のデータを捨てる前に呼ぶ。
            //! 止めないと、XAudio2 のスレッドが捨てたデータを読み続ける。無効な装置では何もしない
            void Suspend() noexcept;

            //! SoundClip が音を作る時に使う。無効な装置では nullptr
            [[nodiscard]] DirectX::AudioEngine* Engine() const noexcept
            {
                return m_engine.get();
            }

        private:
            std::unique_ptr<DirectX::AudioEngine> m_engine;
            int m_silentFrames = 0; // 前に繋ぎ直しを試してからの Update の回数
            bool m_lost = false;    // 機器を失ってから、まだ繋ぎ直しを試していない間
        };
    } // namespace Audio
} // namespace NS
