#pragma once

/// @file Animation.h
/// @brief NS::Graphics::AnimationClip — スケルタルアニメのキーフレームクリップと sampling
///
/// @details ボーンごとの TRS キーフレームトラックを保持し、 時刻 t でポーズ (BonePose 配列) を評価する
/// 補間は LINEAR / STEP に対応 (CUBICSPLINE は非対応)。 評価結果は Skeleton::ComputePalette に渡して
/// SkeletalMesh の描画ポーズにする。 sampling は純関数で、 skinned 頂点シェーダの前段として単体テスト可能

#include "Framework/Graphics/Skeleton.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace NS::Graphics
{
    /// キーフレーム間の補間方式 (glTF の LINEAR / STEP に対応)
    enum class Interpolation
    {
        Linear,
        Step,
    };

    /// 1 ボーンの TRS キーフレームトラック。 各チャンネルは独立した時刻列・補間方式を持つ
    /// 空のチャンネルはそのボーンの bindLocal 値を据え置く意味になる
    struct BoneTrack
    {
        int boneIndex = -1;

        std::vector<float> positionTimes;
        std::vector<NS::Math::Vector3> positionValues;
        Interpolation positionInterp = Interpolation::Linear;

        std::vector<float> rotationTimes;
        std::vector<NS::Math::Quaternion> rotationValues;
        Interpolation rotationInterp = Interpolation::Linear;

        std::vector<float> scaleTimes;
        std::vector<NS::Math::Vector3> scaleValues;
        Interpolation scaleInterp = Interpolation::Linear;
    };

    /// 1 本のアニメーションクリップ (名前・尺・動くボーンのトラック群)
    struct AnimationClip
    {
        std::string name;
        float duration = 0.0f;
        std::vector<BoneTrack> tracks;

        /// 尺が正でトラックが 1 本以上あれば true
        [[nodiscard]] bool IsValid() const noexcept { return duration > 0.0f && !tracks.empty(); }
    };

    /// Vector3 トラックを時刻 t で評価する
    /// Linear=線形補間、 Step=左キー保持、 範囲外=端点クランプ。 times が空なら fallback を返す
    [[nodiscard]] NS::Math::Vector3 SampleVec3(std::span<const float> times,
                                               std::span<const NS::Math::Vector3> values,
                                               Interpolation interp,
                                               float t,
                                               const NS::Math::Vector3& fallback) noexcept;

    /// Quaternion トラックを時刻 t で評価する
    /// Linear=最短経路 slerp、 Step=左キー保持、 範囲外=端点クランプ。 times が空なら fallback を返す
    [[nodiscard]] NS::Math::Quaternion SampleQuat(std::span<const float> times,
                                                  std::span<const NS::Math::Quaternion> values,
                                                  Interpolation interp,
                                                  float t,
                                                  const NS::Math::Quaternion& fallback) noexcept;

    /// clip を時刻 t で評価し pose を埋める。 トラックを持たないボーンは skeleton の bindLocal を使う
    /// @post outPose.size() == skeleton.BoneCount()
    void SampleClipPose(const AnimationClip& clip, const Skeleton& skeleton, float t, std::vector<BonePose>& outPose);

} // namespace NS::Graphics
