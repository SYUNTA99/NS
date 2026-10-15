#pragma once

#include "NSlib/Graphics/Animation.h"
#include "NSlib/Graphics/Skeleton.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        //! 骨名からリグ固有の接頭辞を外して照合用に正規化する
        //! 最後の : か | までを取り除き、残りを前後の空白なしの小文字にする
        [[nodiscard]] std::string NormalizeBoneName(std::string_view raw);

        //! 正規化した骨名でトラックを張り替える。一致しないトラックと空のクリップは除く
        //! 対象の骨名が重複した場合は先の骨を使い、警告する
        [[nodiscard]] std::vector<AnimationClip> BindClipsByName(const std::vector<AnimationClip>& sourceClips,
                                                                 const Skeleton& sourceSkeleton,
                                                                 const Skeleton& targetSkeleton);
    } // namespace Gfx
} // namespace NS
