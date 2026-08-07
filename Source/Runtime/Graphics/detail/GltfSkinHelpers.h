#pragma once

// glTF skin 取込の純粋ヘルパ。 重み正規化 / RH→LH 変換 / topological 整列を cgltf 非依存で行う
// RH→LH は S = diag(1,1,-1) の相似変換 (position の Z 反転、 回転の x,y 反転、 inverseBind の S*M*S 共役)

#include "Runtime/Graphics/Skeleton.h"
#include "Runtime/Core/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace NS::Graphics::detail
{
    /// joint weights を合計 1 へ正規化する。 合計が極小なら joints[0]=0, weights={1,0,0,0} にする
    inline void NormalizeJointWeights(std::array<std::uint32_t, 4>& joints, std::array<float, 4>& weights) noexcept
    {
        const float sum = weights[0] + weights[1] + weights[2] + weights[3];
        if (sum < 1e-8f)
        {
            joints = {0u, 0u, 0u, 0u};
            weights = {1.0f, 0.0f, 0.0f, 0.0f};
            return;
        }
        const float inv = 1.0f / sum;
        for (float& weight : weights)
        {
            weight *= inv;
        }
    }

    /// position / translation を Z 反転で RH→LH 変換する
    [[nodiscard]] inline NS::Core::Vector3 MirrorZ(const NS::Core::Vector3& v) noexcept
    {
        return NS::Core::Vector3{v.x, v.y, -v.z};
    }

    /// 回転 quaternion を RH→LH 変換する。 Z 反転の相似変換では (x,y,z,w) → (-x,-y,z,w)
    [[nodiscard]] inline NS::Core::Quaternion MirrorQuaternionZ(const NS::Core::Quaternion& q) noexcept
    {
        return NS::Core::Quaternion{-q.x, -q.y, q.z, q.w};
    }

    /// cgltf 列優先 16 float を行ベクトル Matrix に読む。列優先 M == 行優先 Mᵀ なので順序そのままでよく、translation は
    /// _41/_42/_43 に入る
    [[nodiscard]] inline NS::Core::Matrix ReadColumnMajorMatrix(const float m[16]) noexcept
    {
        return NS::Core::Matrix{
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
    }

    /// 行列を Z 反転で相似変換し inverseBind を RH→LH する。 S = diag(1,1,-1) として S * M * S
    [[nodiscard]] inline NS::Core::Matrix ConjugateZMatrix(const NS::Core::Matrix& m) noexcept
    {
        const NS::Core::Matrix s = NS::Core::Matrix::CreateScale(1.0f, 1.0f, -1.0f);
        return s * m * s;
    }

    /// bones を親が子より前に来る順へ並べ替え、 old→new index の remap を返す
    /// 出力 bones の parentIndex は new index に張り替え済。 呼出側は remap で頂点 joint index を変換する
    [[nodiscard]] inline std::vector<std::uint32_t> TopologicalSortBones(std::vector<Bone>& bones) noexcept
    {
        const std::size_t boneCount = bones.size();
        std::vector<std::uint32_t> oldToNew(boneCount, 0u);
        std::vector<bool> emitted(boneCount, false);
        std::vector<Bone> sorted;
        sorted.reserve(boneCount);

        bool progress = true;
        while (sorted.size() < boneCount && progress)
        {
            progress = false;
            for (std::size_t i = 0; i < boneCount; ++i)
            {
                if (emitted[i])
                {
                    continue;
                }
                const int parent = bones[i].parentIndex;
                if (parent < 0 || emitted[static_cast<std::size_t>(parent)])
                {
                    oldToNew[i] = static_cast<std::uint32_t>(sorted.size());
                    sorted.push_back(bones[i]);
                    emitted[i] = true;
                    progress = true;
                }
            }
        }

        // 循環など未処理が残っても取りこぼさず末尾に積む。健全な skin では発生しない
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            if (!emitted[i])
            {
                oldToNew[i] = static_cast<std::uint32_t>(sorted.size());
                sorted.push_back(bones[i]);
                emitted[i] = true;
            }
        }

        for (Bone& bone : sorted)
        {
            if (bone.parentIndex >= 0)
            {
                bone.parentIndex = static_cast<int>(oldToNew[static_cast<std::size_t>(bone.parentIndex)]);
            }
        }
        bones = std::move(sorted);
        return oldToNew;
    }
} // namespace NS::Graphics::detail
