#pragma once

/// @file Skeleton.h
/// @brief NS::Graphics::Skeleton — ボーン階層と inverse bind を保持し、ポーズからボーンパレットを計算する
///
/// @details SkeletalMesh の GPU スキニング用。 ボーン配列は親が子より前という topological 順であることを前提に、
/// 各ボーンの local TRS であるポーズからスキニング行列群 palette[k] = inverseBind[k] * jointWorld[k] を
/// 単一前進パスで計算する。 座標規約は p * M の行ベクトル・ LH で、 既存 Transform と同一
/// LBS の CPU 参照スキニングも提供し、 skinned 頂点シェーダはこの参照と同じ数式をミラーする

#include "Framework/Graphics/SkeletalMesh.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace NS::Graphics
{
    /// ボーン 1 本ぶんの local 変換で pose の単位。 既定は恒等変換
    struct BonePose
    {
        NS::Math::Vector3 translation{0.0f, 0.0f, 0.0f};
        NS::Math::Quaternion rotation{}; // 既定は恒等回転で 0,0,0,1
        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    /// スケルトンの 1 ボーン
    /// parentIndex は配列内の親 index で -1 なら root。 配列は親が子より前という topological 順
    struct Bone
    {
        int parentIndex = -1;
        NS::Math::Matrix inverseBind{}; // model→bone で LH・行ベクトル。 既定は恒等
        BonePose bindLocal{};           // 既定ポーズで pose 未指定時に使う
        std::string name;               // glTF node 名でリターゲットの対応づけ鍵
    };

    /// ボーン階層 + inverse bind を保持し、 ポーズ → ボーンパレット行列群を計算する
    /// ボーン配列は親が子より前という topological 順であること。 runtime を単一前進パスにするため
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

        /// bindLocal をポーズとして使ったパレット。 root 上位変換が恒等のスキンでは全要素が恒等になる
        /// SkeletalMesh の既定 (pose 未指定) 描画に使う
        void ComputeBindPalette(std::vector<NS::Math::Matrix>& out) const;

        /// pose から各ボーンの model 空間変換を返す (inverseBind なし)。リターゲット用途向け
        /// @post out.size() == BoneCount()。pose size 不一致時は NS_LOG_ERROR の上 out を恒等で埋める
        void ComputeGlobals(std::span<const BonePose> pose,
                            std::vector<NS::Math::Matrix>& out,
                            bool applyRootTransform = true) const;

        /// root ボーンの親ワールド変換。 アーマチュア変換等を skinned 出力へ反映するために使う
        void SetRootTransform(const NS::Math::Matrix& transform) noexcept;
        [[nodiscard]] const NS::Math::Matrix& RootTransform() const noexcept;

        /// CPU 参照 LBS。Σ weights[i] * (position * palette[joints[i]]) を返す。 範囲外 joint と weight 0 は無視する
        [[nodiscard]] static NS::Math::Vector3 SkinPositionReference(
            const SkinnedVertex& vertex, std::span<const NS::Math::Matrix> palette) noexcept;

    private:
        std::vector<Bone> m_bones;
        NS::Math::Matrix m_rootTransform{}; // skeleton より上のノード変換、 既定は恒等
    };

} // namespace NS::Graphics
