#pragma once

/// @file Retarget.h
/// @brief NS::Graphics — アニメのリターゲット (骨名正規化 / 人型プロファイル / クリップ適用)
///
/// @details 別 glTF のアニメを、別々に読み込んだキャラ骨格へ適用するための補助群
/// 第1段は骨名一致での再 index、 第2段は人型プロファイル + rest 差補正 + 体型吸収
/// 変換は読込時に 1 回 bake し、 出力は既存 AnimationClip (ターゲット骨 index) に揃える

#include "Framework/Graphics/Animation.h"
#include "Framework/Graphics/Skeleton.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Graphics
{
    /// 骨名を正規化する (`mixamorig:` 等の prefix 除去 + 小文字化)
    [[nodiscard]] std::string NormalizeBoneName(std::string_view raw);

    /// 標準人型ボーン (リターゲットの対応づけプロファイル)
    /// Count は要素数であると同時に「該当なし」を表すセンチネルで、 GuessHumanoidBone が一致なし時に返す
    enum class HumanoidBone : int
    {
        Hips,
        Spine,
        Chest,
        Neck,
        Head,
        LeftShoulder,
        LeftUpperArm,
        LeftLowerArm,
        LeftHand,
        RightShoulder,
        RightUpperArm,
        RightLowerArm,
        RightHand,
        LeftUpperLeg,
        LeftLowerLeg,
        LeftFoot,
        RightUpperLeg,
        RightLowerLeg,
        RightFoot,
        Count
    };

    /// HumanoidBone → 骨格内の bone index 対応表。 対応する骨が無ければ -1
    struct HumanoidMap
    {
        std::array<int, static_cast<std::size_t>(HumanoidBone::Count)> boneIndex{};
    };

    /// 正規化済み骨名から標準人型ボーンを推定する。一致なしは HumanoidBone::Count
    [[nodiscard]] HumanoidBone GuessHumanoidBone(std::string_view normalized) noexcept;

    /// 骨格の各骨を NormalizeBoneName → GuessHumanoidBone で人型ボーンへ割り当てる
    /// 同じ人型ボーンに複数該当する場合は配列で先 (親側) を優先する。 未対応スロットは -1
    [[nodiscard]] HumanoidMap BuildHumanoidMap(const Skeleton& skeleton);

    /// 骨名一致で clips の骨 index を target へ張り替える (回転変換なし、一致しないトラックは破棄)
    [[nodiscard]] std::vector<AnimationClip> BindClipsByName(const Skeleton& source,
                                                             const std::vector<AnimationClip>& clips,
                                                             const Skeleton& target);

    /// 人型プロファイル + rest 差補正で異なるリグの clips を target へリターゲットする
    /// 人型ボーンが 1 つも対応づかない場合は空を返す
    [[nodiscard]] std::vector<AnimationClip> RetargetClips(const Skeleton& source,
                                                           const std::vector<AnimationClip>& clips,
                                                           const Skeleton& target,
                                                           float resampleFps = 30.0f);

    /// アニメ glTF を読み、 target 骨格に適用できる AnimationClip 群を返すファサード
    /// 第1段は同名リグの再 index (`BindClipsByName`)。 読込失敗や一致なしでは空を返す
    [[nodiscard]] std::vector<AnimationClip> LoadAnimationsForSkeleton(const std::string& path, const Skeleton& target);
} // namespace NS::Graphics
