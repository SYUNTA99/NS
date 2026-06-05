#pragma once

/// @file Retarget.h
/// @brief NS::Graphics — アニメのリターゲット (骨名正規化 / 人型プロファイル / クリップ適用)
///
/// @details 別 glTF のアニメを、別々に読み込んだキャラ骨格へ適用するための補助群
/// 第1段は骨名一致での再 index、 第2段は人型プロファイル + rest 差補正 + 体型吸収
/// 変換は読込時に 1 回 bake し、 出力は既存 AnimationClip (ターゲット骨 index) に揃える

#include "Framework/Graphics/Animation.h"
#include "Framework/Graphics/Skeleton.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Graphics
{
    /// 骨名を対応づけ用に正規化する
    /// 名前空間 prefix (`mixamorig:` / `Armature|` 等、 最後の ':' か '|' より後ろ) を除去し、
    /// 前後空白を落として小文字化する。 空入力には空文字を返す
    [[nodiscard]] std::string NormalizeBoneName(std::string_view raw);

    /// 同一リグ前提でソース clips のトラックを「正規化骨名の一致」で target 骨 index へ張り替える (第1段)
    /// 回転変換はしない (rest 一致前提)。 一致しないトラックは捨て、 トラック 0 のクリップは除外する
    /// 出力は target 骨格 index の AnimationClip 群で、 そのまま SkeletalAnimationComponent に渡せる
    [[nodiscard]] std::vector<AnimationClip> BindClipsByName(const Skeleton& source,
                                                             const std::vector<AnimationClip>& clips,
                                                             const Skeleton& target);

    /// アニメ glTF を読み、 target 骨格に適用できる AnimationClip 群を返すファサード
    /// 第1段は同名リグの再 index (`BindClipsByName`)。 読込失敗や一致なしでは空を返す
    [[nodiscard]] std::vector<AnimationClip> LoadAnimationsForSkeleton(const std::string& path, const Skeleton& target);
} // namespace NS::Graphics
