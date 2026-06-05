#pragma once

/// @file gltf_skin_helpers.h
/// @brief glTF skin 取込の純粋ヘルパ (重み正規化 / RH→LH / topological 整列)
///
/// @details LoadGltfSkinnedMesh が使う座標変換・正規化・並べ替えを cgltf 非依存の free 関数として切り出し、
/// 単体テスト可能にする。 RH→LH は S = diag(1,1,-1) の相似変換 (position / translation の Z 反転、
/// 回転 quaternion の x,y 反転、 inverseBind の S*M*S 共役) で行う。 detail 内部実装ヘッダ

#include "Framework/Graphics/Skeleton.h"
#include "Framework/Math/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace NS::Graphics::detail
{
    /// joint weights を合計 1 へ正規化する。 合計が極小なら joints[0]=0, weights={1,0,0,0} に倒す
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

    /// position / translation を RH→LH 変換する (Z 反転)
    [[nodiscard]] inline NS::Math::Vector3 MirrorZ(const NS::Math::Vector3& v) noexcept
    {
        return NS::Math::Vector3{v.x, v.y, -v.z};
    }

    /// 回転 quaternion を RH→LH 変換する。 Z 反転の相似変換では (x,y,z,w) → (-x,-y,z,w)
    [[nodiscard]] inline NS::Math::Quaternion MirrorQuaternionZ(const NS::Math::Quaternion& q) noexcept
    {
        return NS::Math::Quaternion{-q.x, -q.y, q.z, q.w};
    }

    /// cgltf の列優先 16 float を NS の row-major 行ベクトル Matrix に読む
    /// 列優先 M の格納列は行優先 Mᵀ の格納と一致し、 行ベクトル規約 (p * M) で必要なのは Mᵀ なので
    /// 結果として 16 float を順序そのまま写すのが正しい (translation は _41/_42/_43 に入る)
    [[nodiscard]] inline NS::Math::Matrix ReadColumnMajorMatrix(const float m[16]) noexcept
    {
        return NS::Math::Matrix{
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
    }

    /// 行列を Z 反転で相似変換する (inverseBind の RH→LH)。 S = diag(1,1,-1) として S * M * S
    [[nodiscard]] inline NS::Math::Matrix ConjugateZMatrix(const NS::Math::Matrix& m) noexcept
    {
        const NS::Math::Matrix s = NS::Math::Matrix::CreateScale(1.0f, 1.0f, -1.0f);
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

        // 循環など未処理が残っても取りこぼさず末尾に積む (健全な skin では発生しない)
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
