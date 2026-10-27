#pragma once

#include <string>

namespace NS
{
    /// エフェクトの絵を名前で指す欄の値
    /// 欄は名前だけを持ち、絵を読むのは PreloadEffects と PlayEffect から渡された EffectKeeper。
    /// 保存とインスペクタは名前の文字列を扱い、インスペクタはエフェクトの一覧から選ばせる
    struct EffectName
    {
        std::string name; // Assets/Effects からの相対の名前。拡張子 .efkefc を除く
    };
} // namespace NS
