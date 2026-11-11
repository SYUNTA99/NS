#pragma once

#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Core/NonCopyable.h"

namespace NS
{
    class Sound;

    //! シーンの曲。1 本をループで鳴らし、止めるまで鳴らし続ける
    //! 鳴らす先も曲の素材も覚えず、呼ぶたびに受ける。いつ始めるかは Scene の派生が決める。
    //! 音の機器を繋ぎ直した後の鳴らし直しは、鳴らす先の Audio::SoundScene::Update が行う
    class MusicDirector final : public NonCopyable
    {
    public:
        //! @brief 曲をループで鳴らし始める
        //! @details sounds が nullptr (音の装置を受け取っていないシーン)・music が nullptr の時は、
        //!          音を出さずに始めた事にする
        //! @param[in] sounds 鳴らす先。Stop に同じ物を渡す事
        //! @param[in] music 鳴らす曲。AssetManager が持つ素材で、Stop まで生きている事
        //! @param[in] volume 音量。Audio::SoundScene::Play に渡す
        //! @return 今の呼び出しで始めたか。Stop までの間にもう一度呼ぶと false で、曲は続く
        bool Start(Audio::SoundScene* sounds, const Sound* music, float volume);

        //! 曲を止める。次の Start は最初から鳴らす
        void Stop(Audio::SoundScene* sounds) noexcept;

        //! Start から Stop までの間か。音を出さずに始めた時も true
        [[nodiscard]] bool IsStarted() const noexcept { return m_started; }

    private:
        Audio::SoundHandle m_handle{}; // 音を出さずに始めた時は無効
        bool m_started = false;
    };
} // namespace NS
