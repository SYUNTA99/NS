#pragma once

#include "NSlib/Core/Types.h"

namespace NS
{
    class Sound;
    class SoundObj;

    namespace Audio
    {
        class AudioDevice;
    }

    //! アクターの音の部品と装置を取得するインターフェース。操作は下の関数で行う
    class IGetAudio
    {
    public:
        //! 鳴らし続ける音を受け持つ部品。枠 Sound を作っていない個体では nullptr
        [[nodiscard]] virtual SoundObj* GetSound() const noexcept = 0;
        //! 音を鳴らす装置。シーンに居ない間と、装置を受け取っていないシーンでは nullptr
        [[nodiscard]] virtual Audio::AudioDevice* GetAudioDevice() const noexcept = 0;

    protected:
        ~IGetAudio() = default;
    };

    //! @brief 音を 1 回鳴らす。空の素材・nullptr・シーンが装置を持たない時は何もしない
    //! @param[in] sound 鳴らす音の素材
    //! @param[in] volume Audio::XA2Sound::Play に渡す音量
    void PlaySound(const IGetAudio* user, const Sound* sound, float volume);

    //! @brief 音をループで鳴らし始め、止めるまで鳴らし続ける。アクターが世界から外れる時は基底が止める
    //! @param[in] sound 鳴らす音の素材
    //! @param[in] volume Audio::SoundLoop::SetVolume に渡す音量
    //! @return 音の番号。Sound が無い時・nullptr・空の道筋は 0
    [[nodiscard]] uint32 PlayLoopSound(const IGetAudio* user, const Sound* sound, float volume);

    //! PlayLoopSound で鳴らした音を止める。無い番号と Sound が無い時は何もしない
    void StopSound(const IGetAudio* user, uint32 id) noexcept;

    //! PlayLoopSound で鳴らした音の音量を変える。無い番号と Sound が無い時は何もしない
    void SetSoundVolume(const IGetAudio* user, uint32 id, float volume) noexcept;
} // namespace NS
