#pragma once

#include "NSlib/CoreObject/Object.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace NS
{
    class AssetManager;
    class SoundClip;

    //! @brief アクターが読んだ音を名前で覚える部品
    //! @details 音の持ち主は AssetManager で、この部品は借りるだけ。利用側は IGetAudio の関数を使う
    class Sound : public Object
    {
    public:
        //! @brief 名前の音を assets から読んで覚える。覚えた名前と空の名前では何もしない
        //! @param[in] name Assets/Sounds からの相対の名前。拡張子 .wav を除く
        void Preload(AssetManager* assets, std::string_view name);

        //! @brief 覚えた名前の音を 1 回鳴らす
        //! @details 覚えていない名前は、名前ごとに 1 回だけ WARN を出して鳴らさない。
        //!          読めなかった音と空の名前は黙って鳴らさない
        //! @param[in] volume Audio::SoundClip::Play に渡す音量
        void Play(std::string_view name, float volume);

        NS_REFLECT_NONE(Sound, Object)

    private:
        // 鳴らすたびに名前の文字列を作らずに引けるよう、std::less<> で std::string_view から探す。
        // 覚えていない名前を鳴らした時は nullptr で入る
        std::map<std::string, SoundClip*, std::less<>> m_sounds;
    };
} // namespace NS
