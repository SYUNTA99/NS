#pragma once

#include "NSlib/CoreObject/Reflection/SoundName.h"

#include <initializer_list>

namespace NS
{
    class Sound;

    namespace Audio
    {
        class AudioDevice;
    }

    //! アクターの音の部品と装置を取得するインターフェース。操作は下の関数で行う
    class IUseAudio
    {
    public:
        //! 読んだ音を覚える部品。部品 Sound を作っていないアクターでは nullptr
        [[nodiscard]] virtual Sound* GetSound() const noexcept = 0;
        //! 音を鳴らす装置。シーンに居ない間と、装置を受け取っていないシーンでは nullptr
        [[nodiscard]] virtual Audio::AudioDevice* GetAudioDevice() const noexcept = 0;

    protected:
        ~IUseAudio() = default;
    };

    //! @brief 鳴らす名前の音を先に読む。Sound が無ければ何もしない
    //! @details AssetManager::Get() の装置で読み、シーンの装置は見ない。シーンが装置を受け取る前の OnStart で呼んでよい
    void PreloadSounds(const IUseAudio* user, std::initializer_list<SoundName> names);

    //! @brief 音を 1 回鳴らす。Sound が無い時と、シーンが装置を持たない時は何もしない
    //! @param[in] name PreloadSounds に渡した名前
    //! @param[in] volume Audio::SoundClip::Play に渡す音量
    void PlaySound(const IUseAudio* user, const SoundName& name, float volume);
} // namespace NS
