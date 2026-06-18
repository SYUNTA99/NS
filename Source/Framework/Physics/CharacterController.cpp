#include "Framework/Physics/CharacterController.h"

#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Physics/CollisionGrid.h"
#include "Framework/Physics/SweptAABB.h"
#include "Framework/Physics/SweptOBB.h"
#include "Framework/Physics/SweptTriangle.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr int kMaxSubSteps = 4;
    // 壁との安全マージン。小さすぎると毎フレーム toi=0 で hit が連続して進まなくなる (引っかかり)
    // 1cm 離れて stop することで次フレームの slide motion が確実に進む
    constexpr float kSkin = 0.01f;
    // 着地直後に player.y が跳ねて grounded がちらつくのを抑える許容距離
    // raycast を少し下まで延ばして接地を拾い続け、grounded 判定の点滅を防ぐ補助
    constexpr float kGroundProbeDistance = 0.2f;
    // walkable 床とみなす normal.y の閾値。cos 45 ≈ 0.707、45° 含むため 0.7
    constexpr float kFloorNormalY = 0.7f;

    /// capsule が motion だけ動く間に占有する swept AABB を返す。 grid 候補絞り込みの query box に使う
    /// 縦 capsule (axis=Y) 前提で、 XZ は radius、 Y は radius + halfHeight 膨張させる
    [[nodiscard]] NS::Math::AABB CapsuleSweptAabb(const NS::Physics::Capsule& cap,
                                                  const NS::Math::Vector3& motion) noexcept
    {
        const float rx = cap.radius;
        const float ry = cap.radius + cap.halfHeight;
        const float rz = cap.radius;
        const NS::Math::Vector3 a = cap.center;
        const NS::Math::Vector3 b = cap.center + motion;
        const float minX = std::min(a.x, b.x) - rx;
        const float maxX = std::max(a.x, b.x) + rx;
        const float minY = std::min(a.y, b.y) - ry;
        const float maxY = std::max(a.y, b.y) + ry;
        const float minZ = std::min(a.z, b.z) - rz;
        const float maxZ = std::max(a.z, b.z) + rz;
        NS::Math::AABB q;
        q.Center = NS::Math::Vector3{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
        q.Extents = NS::Math::Vector3{(maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f};
        return q;
    }
} // namespace

namespace NS::Physics
{
    CharacterControllerResult CharacterController::Update(const CharacterControllerInput& input) noexcept
    {
        NS_SCOPED_TIMER(::NS::Core::LogCat::Game, "CharacterController::Update");

        CharacterControllerResult result;
        result.position = input.position;
        result.velocity = input.velocity;
        result.grounded = false;
        result.contactNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};

        if (!std::isfinite(input.dt) || input.dt <= 0.0f || !std::isfinite(input.capsuleRadius) ||
            input.capsuleRadius < 0.0f || !std::isfinite(input.capsuleHalfHeight) || input.capsuleHalfHeight < 0.0f)
            return result;

        const float subDt = input.dt / static_cast<float>(kMaxSubSteps);

        std::vector<std::uint32_t> candidates; // grid broad-phase の候補バッファ (substep 間で使い回す)

        for (int step = 0; step < kMaxSubSteps; ++step)
        {
            // substep 内で hit -> slide -> 残り motion で再 swept を最大 kMaxSlideIters 回チェイン
            // これで床に接触したまま壁に走った時にも壁 hit が無視されず stop する
            constexpr int kMaxSlideIters = 4;
            float remainingTime = 1.0f; // この substep のうち未消費の比率 (0..1)

            for (int slideIter = 0; slideIter < kMaxSlideIters && remainingTime > 0.0f; ++slideIter)
            {
                NS::Math::Vector3 motion = result.velocity * (subDt * remainingTime);

                if (std::abs(motion.x) < 1e-9f && std::abs(motion.y) < 1e-9f && std::abs(motion.z) < 1e-9f)
                    break;

                Capsule cap;
                cap.center = result.position;
                cap.axis = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
                cap.halfHeight = input.capsuleHalfHeight;
                cap.radius = input.capsuleRadius;

                float earliestToi = 1.0f;
                NS::Math::Vector3 hitNormal{0.0f, 0.0f, 0.0f};
                bool anyHit = false;

                // grid があれば capsule の swept AABB 近傍だけを narrow phase に掛ける (無ければ総当たり)
                if (input.grid != nullptr && !input.grid->IsEmpty())
                {
                    const NS::Math::AABB queryBox = CapsuleSweptAabb(cap, motion);
                    input.grid->Query(queryBox, candidates);
                    for (const std::uint32_t idx : candidates)
                    {
                        float toi = 1.0f;
                        NS::Math::Vector3 n{};
                        if (SweptCapsuleVsAABB(cap, motion, input.world[idx], toi, n))
                        {
                            if (toi < earliestToi)
                            {
                                earliestToi = toi;
                                hitNormal = n;
                                anyHit = true;
                            }
                        }
                    }
                }
                else
                {
                    for (const NS::Math::AABB& box : input.world)
                    {
                        float toi = 1.0f;
                        NS::Math::Vector3 n{};
                        if (SweptCapsuleVsAABB(cap, motion, box, toi, n))
                        {
                            if (toi < earliestToi)
                            {
                                earliestToi = toi;
                                hitNormal = n;
                                anyHit = true;
                            }
                        }
                    }
                }

                // AABB と Triangle を同 substep 内で sweep し最小 TOI 側で slide させる
                for (const Triangle& tri : input.worldTriangles)
                {
                    float toi = 1.0f;
                    NS::Math::Vector3 n{};
                    if (SweptCapsuleVsTriangle(cap, motion, tri, toi, n))
                    {
                        if (toi < earliestToi)
                        {
                            earliestToi = toi;
                            hitNormal = n;
                            anyHit = true;
                        }
                    }
                }

                // 自由配置物の OBB を同 substep 内で sweep し最小 TOI 側で slide させる
                for (const OBB& obb : input.worldObbs)
                {
                    float toi = 1.0f;
                    NS::Math::Vector3 n{};
                    if (SweptCapsuleVsOBB(cap, motion, obb, toi, n))
                    {
                        if (toi < earliestToi)
                        {
                            earliestToi = toi;
                            hitNormal = n;
                            anyHit = true;
                        }
                    }
                }

                if (!anyHit)
                {
                    result.position = result.position + motion;
                    remainingTime = 0.0f;
                    break;
                }

                const float safeToi = std::max(0.0f, earliestToi - kSkin);
                result.position = result.position + motion * safeToi;

                const float vDotN =
                    result.velocity.x * hitNormal.x + result.velocity.y * hitNormal.y + result.velocity.z * hitNormal.z;
                if (vDotN < 0.0f)
                {
                    result.velocity.x -= vDotN * hitNormal.x;
                    result.velocity.y -= vDotN * hitNormal.y;
                    result.velocity.z -= vDotN * hitNormal.z;
                }
                result.contactNormal = hitNormal;

                if (hitNormal.y > kFloorNormalY)
                    result.grounded = true;

                remainingTime *= std::max(0.0f, 1.0f - earliestToi);
            }
        }

        // 補助の grounded probe: Capsule 底端から下方向に short ray。床ギリギリで停止した
        // ケースを補足する (slide 後に anyHit が false になり grounded が立たない問題回避)
        if (!result.grounded)
        {
            const NS::Math::Vector3 bottomCenter{
                result.position.x, result.position.y - input.capsuleHalfHeight, result.position.z};
            const NS::Math::Ray ray(bottomCenter, NS::Math::Vector3{0.0f, -1.0f, 0.0f});
            for (const NS::Math::AABB& box : input.world)
            {
                float dist = 0.0f;
                if (ray.Intersects(box, dist) && dist <= input.capsuleRadius + kGroundProbeDistance)
                {
                    result.grounded = true;
                    break;
                }
            }

            for (const OBB& obb : input.worldObbs)
            {
                const NS::Math::Vector3 d = bottomCenter - obb.center;
                const NS::Math::Vector3 localOrigin{d.Dot(obb.axisX), d.Dot(obb.axisY), d.Dot(obb.axisZ)};
                const NS::Math::Vector3 down{0.0f, -1.0f, 0.0f};
                const NS::Math::Vector3 localDir{down.Dot(obb.axisX), down.Dot(obb.axisY), down.Dot(obb.axisZ)};
                const NS::Math::Ray localRay(localOrigin, localDir);
                const NS::Math::AABB localBox(NS::Math::Vector3{0.0f, 0.0f, 0.0f}, obb.halfExtents);
                float dist = 0.0f;
                if (localRay.Intersects(localBox, dist) && dist <= input.capsuleRadius + kGroundProbeDistance)
                {
                    result.grounded = true;
                    break;
                }
            }
        }

        return result;
    }
} // namespace NS::Physics
