#pragma once

#include "NSlib/Core/Types.h"

namespace NS
{
    class Sound;
    class SoundObj;

    namespace Audio
    {
        class SoundScene;
    }

    //! アクターの音の部品と、鳴らす先を取得するインターフェース。操作は下の関数で行う
    class IGetAudio
    {
    public:
        //! 音を受け持つ部品。枠 Sound を作っていない個体では nullptr
        [[nodiscard]] virtual SoundObj* GetSound() const noexcept = 0;
        //! 鳴っている音を持つ SoundScene。シーンに居ない間と、音の装置を受け取っていないシーンでは nullptr
        [[nodiscard]] virtual Audio::SoundScene* GetSoundScene() const noexcept = 0;

    protected:
        ~IGetAudio() = default;
    };

    //! @brief 音を鳴らす。アクターが世界から外れる時は、基底がループの音を止める
    //! @param[in] sound 鳴らす音の素材
    //! @param[in] volume Audio::SoundScene::Play に渡す音量
    //! @param[in] loop true なら StopSound まで繰り返す。false なら 1 回鳴らす
    //! @return 音の番号。捨ててよい。Sound が無い時・nullptr・空の道筋は 0
    uint32 PlaySound(const IGetAudio* user, const Sound* sound, float volume, bool loop = false);

    //! PlaySound で鳴らした音を止める。無い番号と Sound が無い時は何もしない
    void StopSound(const IGetAudio* user, uint32 id) noexcept;

    //! PlaySound で鳴らした音の音量を変える。無い番号と Sound が無い時は何もしない
    void SetSoundVolume(const IGetAudio* user, uint32 id, float volume);
} // namespace NS
