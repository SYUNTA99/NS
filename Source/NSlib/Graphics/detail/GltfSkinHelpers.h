#pragma once

// glTF の骨格取り込み用。重みの正規化・座標系の変換・親優先の並べ替えを cgltf に依存せず行う
// 右手系から左手系への変換は S = diag(1,1,-1) で相似変換する。位置の Z と回転の x,y を反転する

#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/Skeleton.h"

#include "NSlib/Core/Types.h"
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            //! joint weights を合計 1 へ正規化する。合計が極小なら joints[0]=0, weights={1,0,0,0} にする
            inline void NormalizeJointWeights(std::array<uint32, 4>& joints, std::array<float, 4>& weights) noexcept
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

            //! position / translation を Z 反転で右手系から左手系へ変換する
            [[nodiscard]] inline Vector3 MirrorZ(const Vector3& v) noexcept
            {
                return Vector3{v.x, v.y, -v.z};
            }

            //! 回転クォータニオンを右手系から左手系へ変換する。Z 反転の相似変換では (x,y,z,w) → (-x,-y,z,w)
            [[nodiscard]] inline Quaternion MirrorQuaternionZ(const Quaternion& q) noexcept
            {
                return Quaternion{-q.x, -q.y, q.z, q.w};
            }

            //! cgltf 列優先 16 float を行ベクトル Matrix に読む。列優先 M == 行優先 Mᵀ
            //! なので順序そのままでよく、translation は _41/_42/_43 に入る
            [[nodiscard]] inline Matrix ReadColumnMajorMatrix(const float m[16]) noexcept
            {
                return Matrix{m[0],
                                  m[1],
                                  m[2],
                                  m[3],
                                  m[4],
                                  m[5],
                                  m[6],
                                  m[7],
                                  m[8],
                                  m[9],
                                  m[10],
                                  m[11],
                                  m[12],
                                  m[13],
                                  m[14],
                                  m[15]};
            }

            //! inverseBind を Z 反転で右手系から左手系へ変換する。S = diag(1,1,-1) として S * M * S
            [[nodiscard]] inline Matrix ConjugateZMatrix(const Matrix& m) noexcept
            {
                const Matrix s = Matrix::CreateScale(1.0f, 1.0f, -1.0f);
                return s * m * s;
            }

            //! bones を親が子より前に来る順へ並べ替え、old→new index の remap を返す
            //! 出力 bones の parentIndex は new index に張り替え済。呼出側は remap で頂点 joint index を変換する
            [[nodiscard]] inline std::vector<uint32> TopologicalSortBones(std::vector<Bone>& bones) noexcept
            {
                const std::size_t boneCount = bones.size();
                std::vector<uint32> oldToNew(boneCount, 0u);
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
                            oldToNew[i] = static_cast<uint32>(sorted.size());
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
                        oldToNew[i] = static_cast<uint32>(sorted.size());
                        sorted.push_back(bones[i]);
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
        } // namespace detail
    } // namespace Gfx
} // namespace NS
