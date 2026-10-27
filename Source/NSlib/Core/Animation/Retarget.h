#pragma once

#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Animation/Skeleton.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Anim
    {
        /// 骨名からリグ固有の接頭辞を外して照合用に正規化する
        /// 最後の : か | までを取り除き、残りを前後の空白なしの小文字にする
        [[nodiscard]] std::string NormalizeBoneName(std::string_view raw);

        /// clip のトラックごとに、正規化した骨名が一致する skeleton の骨の番号を引く
        /// 戻り値はトラックと同じ並びで、一致する骨が無いトラックは -1
        /// 正規化した骨名が skeleton の中で重なる時は先の骨を使い、警告する
        [[nodiscard]] std::vector<int> FindTrackBones(const AnimClip& clip, const Skeleton& skeleton);
    } // namespace Anim
} // namespace NS
