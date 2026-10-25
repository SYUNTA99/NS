#pragma once

#include <string>

namespace NS
{
    //! @brief 音を名前で指す欄の値
    //! @details 欄は名前だけを持ち、音を読むのは PreloadSounds と PlaySound から渡された Sound。
    //! 保存とインスペクタは名前の文字列を扱い、インスペクタは音の一覧から選ばせる
    struct SoundName
    {
        std::string name; // Assets/Sounds からの相対の名前。拡張子 .wav を除く
    };
} // namespace NS
