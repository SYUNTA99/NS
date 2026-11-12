#include "NSlib/Core/Geometry/SkinnedBounds.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Types.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NS
{
    namespace
    {
        template <typename Fn>
        void ForEachBoneVertex(const SkinnedVertex* vertices,
                               std::size_t vertexCount,
                               std::size_t boneCount,
                               Fn&& fn) noexcept
        {
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                const SkinnedVertex& v = vertices[i];
                for (int k = 0; k < 4; ++k)
                {
                    if (v.weights[k] > 0.0f && v.joints[k] < boneCount)
                    {
                        fn(v.joints[k], v.position);
                    }
                }
            }
        }
    } // namespace

    std::vector<BoneSphere> ComputeBoneSpheres(const SkinnedVertex* vertices,
                                               std::size_t vertexCount,
                                               std::size_t boneCount)
    {
        std::vector<BoneSphere> spheres(boneCount, BoneSphere{.radius = -1.0f});
        if (vertices == nullptr || vertexCount == 0u || boneCount == 0u)
        {
            return spheres;
        }

        constexpr float k_Big = std::numeric_limits<float>::max();
        std::vector<Vector3> mn(boneCount, Vector3{k_Big, k_Big, k_Big});
        std::vector<Vector3> mx(boneCount, Vector3{-k_Big, -k_Big, -k_Big});
        std::vector<bool> hit(boneCount, false);

        // 各頂点を weight>0 の全影響ボーンへ算入し、ボーンごとの軸並行範囲を取る
        ForEachBoneVertex(vertices, vertexCount, boneCount, [&](uint32 j, const Vector3& position) {
            mn[j] = Vector3::Min(mn[j], position);
            mx[j] = Vector3::Max(mx[j], position);
            hit[j] = true;
        });
        for (std::size_t j = 0; j < boneCount; ++j)
        {
            if (hit[j])
            {
                spheres[j].center = (mn[j] + mx[j]) * 0.5f;
            }
        }

        // 中心が決まってから最遠影響頂点までの距離を半径にする
        std::vector<float> maxD2(boneCount, 0.0f);
        ForEachBoneVertex(vertices, vertexCount, boneCount, [&](uint32 j, const Vector3& position) {
            maxD2[j] = std::max(maxD2[j], Vector3::DistanceSquared(position, spheres[j].center));
        });
        for (std::size_t j = 0; j < boneCount; ++j)
        {
            if (hit[j])
            {
                spheres[j].radius = std::sqrt(maxD2[j]);
            }
        }
        return spheres;
    }

    AABB MergeSkinnedBounds(const std::vector<BoneSphere>& spheres,
                            const Matrix* palette,
                            std::size_t paletteCount,
                            const AABB& fallback)
    {
        if (palette == nullptr)
        {
            NS_LOG_ERROR(Graphics, "MergeSkinnedBounds: palette が nullptr");
            return fallback;
        }

        AABB merged{};
        bool any = false;
        const std::size_t count = std::min(spheres.size(), paletteCount);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (spheres[i].radius < 0.0f)
            {
                continue; // 影響頂点なしのボーンは飛ばす
            }

            // 球中心だけ現在ポーズへ動かし半径そのままの箱にする。剛体変換なら半径は変わらない
            const Vector3 c = Vector3::Transform(spheres[i].center, palette[i]);
            const float r = spheres[i].radius;
            const AABB box{c, Vector3{r, r, r}};
            if (!any)
            {
                merged = box;
                any = true;
            }
            else
            {
                AABB::CreateMerged(merged, merged, box);
            }
        }
        if (!any)
        {
            return fallback;
        }
        return merged;
    }
} // namespace NS
