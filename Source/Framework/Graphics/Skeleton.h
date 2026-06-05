#pragma once

/// @file Skeleton.h
/// @brief NS::Graphics::Skeleton — ボーン階層と inverse bind を保持し、ポーズからボーンパレットを計算する
///
/// @details GPU スキニング (SkeletalMesh) 用。 ボーン配列は親が子より前 (topological 順) であることを前提に、
/// ポーズ (各ボーンの local TRS) からスキニング行列群 palette[k] = inverseBind[k] * jointWorld[k] を
/// 単一前進パスで計算する。 座標規約は行ベクトル (p * M)・ LH で、 既存 Transform と同一
/// CPU 参照スキニング (LBS) も提供し、 skinned 頂点シェーダはこの参照と同じ数式をミラーする

#include "Framework/Graphics/SkeletalMesh.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <span>
#include <vector>

namespace NS::Graphics
{
    /// ボーン 1 本ぶんの local 変換 (pose の単位)。 既定は恒等変換
    struct BonePose
    {
        NS::Math::Vector3 translation{0.0f, 0.0f, 0.0f};
        NS::Math::Quaternion rotation{}; // 既定は恒等回転 (0,0,0,1)
        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    /// スケルトンの 1 ボーン
    /// parentIndex は配列内の親 index (-1 = root)。 配列は親が子より前 (topological 順)
    struct Bone
    {
        int parentIndex = -1;
        NS::Math::Matrix inverseBind{}; // model→bone (LH, 行ベクトル)。 既定は恒等
        BonePose bindLocal{};           // 既定ポーズ (pose 未指定時に使う)
    };

    /// ボーン階層 + inverse bind を保持し、 ポーズ → ボーンパレット行列群を計算する
    /// ボーン配列は親が子より前 (topological 順) であること (runtime を単一前進パスにするため)
    class Skeleton
    {
    public:
        Skeleton() = default;
        explicit Skeleton(std::vector<Bone> bones) noexcept;

        [[nodiscard]] std::size_t BoneCount() const noexcept;
        [[nodiscard]] const std::vector<Bone>& Bones() const noexcept;

        /// pose (各ボーンの local TRS、 size は BoneCount と一致) からパレットを計算する
        /// out[k] = inverseBind[k] * jointWorld[k]、 jointWorld[i] = local[i] * jointWorld[parent]
        /// @post out.size() == BoneCount()。 pose size 不一致時は NS_LOG_ERROR の上 out を恒等で埋める
        void ComputePalette(std::span<const BonePose> pose, std::vector<NS::Math::Matrix>& out) const;

        /// bindLocal をポーズとして使ったパレット。 well-formed なスキンでは全要素が恒等になる
        /// SkeletalMesh の既定 (pose 未指定) 描画に使う
        void ComputeBindPalette(std::vector<NS::Math::Matrix>& out) const;

        /// CPU 参照 LBS。 1 頂点を palette で変形して返す (skinned 頂点シェーダと同式)
        /// 戻り値 = Σ weights[i] * (position * palette[joints[i]])。 範囲外 joint と weight 0 は無視
        [[nodiscard]] static NS::Math::Vector3 SkinPositionReference(
            const SkinnedVertex& vertex, std::span<const NS::Math::Matrix> palette) noexcept;

    private:
        std::vector<Bone> m_bones;
    };

} // namespace NS::Graphics
