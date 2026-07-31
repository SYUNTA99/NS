#pragma once

#include "Runtime/Graphics/Animation.h"
#include "Runtime/Graphics/Skeleton.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Graphics
{
    /// 骨名からリグ固有の prefix を外して照合用に正規化する
    /// 最後の `:` か `|` までを取り除き、残りを前後の空白なしの小文字にする
    [[nodiscard]] std::string NormalizeBoneName(std::string_view raw);

    /// @brief クリップの各トラックを、source の骨名と正規化名が一致する target の骨 index へ張替える
    /// @details 一致する骨が無いトラックは落とす。全トラックが落ちたクリップは結果に含めず警告だけ出す
    /// トラックが空のクリップを返すと IsValid() が false で bind ポーズに戻るだけの死にデータになり、
    /// クリップ数の水増しで添字選択もずれるため
    /// target 側で正規化後の名前が重複したときは先の骨へ張り、警告する
    [[nodiscard]] std::vector<AnimationClip> BindClipsByName(const std::vector<AnimationClip>& sourceClips,
                                                             const Skeleton& sourceSkeleton,
                                                             const Skeleton& targetSkeleton);
} // namespace NS::Graphics
