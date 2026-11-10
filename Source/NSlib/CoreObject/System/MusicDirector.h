#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <memory>

namespace NS
{
    class Sound;

    namespace Audio
    {
        class AudioDevice;
        class SoundLoop;
    } // namespace Audio

    //! シーンの曲。1 本をループで鳴らし、止めるまで鳴らし続ける
    //! 装置も曲の素材も覚えず、Start のたびに受ける。いつ始めるかは Scene の派生が決める
    class MusicDirector final : public NonCopyable
    {
    public:
        MusicDirector() noexcept;
        ~MusicDirector() noexcept;

        //! @brief 曲をループで鳴らし始める
        //! @details device が nullptr (装置を受け取っていないシーン)・music が nullptr・音の無い素材の時は、
        //!          音を出さずに始めた事にする
        //! @param[in] music 鳴らす曲。AssetManager が持つ素材で、Stop かこの物の破棄まで生きている事
        //! @param[in] volume 音量。Audio::SoundLoop::SetVolume に渡す
        //! @return 今の呼び出しで始めたか。Stop までの間にもう一度呼ぶと false で、曲は続く
        bool Start(const Audio::AudioDevice* device, const Sound* music, float volume);

        //! 曲を止める。次の Start は最初から鳴らす
        void Stop() noexcept;

        //! 音の機器を繋ぎ直して止まった曲を鳴らし直す。鳴っている間は何もしない。Scene が毎ステップ 1 回呼ぶ
        void Update();

        //! Start から Stop までの間か。音を出さずに始めた時も true
        [[nodiscard]] bool IsStarted() const noexcept { return m_started; }

    private:
        std::unique_ptr<Audio::SoundLoop> m_loop; // 音を出さずに始めた時は nullptr
        bool m_started = false;
    };
} // namespace NS
