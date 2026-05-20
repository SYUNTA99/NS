#include "ns/physics/character_controller.h"

#include "ns/physics/capsule.h"
#include "ns/physics/swept_aabb.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kMaxSubSteps = 4;
    constexpr float kSkin = 0.001f;
    constexpr float kGroundProbeDistance = 0.1f;
} // namespace

namespace ns::physics
{
    CharacterControllerResult CharacterController::Update(const CharacterControllerInput& input) noexcept
    {
        CharacterControllerResult result;
        result.position = input.position;
        result.velocity = input.velocity;
        result.grounded = false;
        result.contactNormal = ns::core::Vector3{0.0f, 0.0f, 0.0f};

        if (input.dt <= 0.0f)
            return result;

        const float subDt = input.dt / static_cast<float>(kMaxSubSteps);

        for (int step = 0; step < kMaxSubSteps; ++step)
        {
            ns::core::Vector3 motion = result.velocity * subDt;

            Capsule cap;
            cap.center = result.position;
            cap.axis = ns::core::Vector3{0.0f, 1.0f, 0.0f};
            cap.halfHeight = input.capsuleHalfHeight;
            cap.radius = input.capsuleRadius;

            float earliestToi = 1.0f;
            ns::core::Vector3 hitNormal{0.0f, 0.0f, 0.0f};
            bool anyHit = false;

            for (const ns::core::AABB& box : input.world)
            {
                float toi = 1.0f;
                ns::core::Vector3 n{};
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

            if (anyHit)
            {
                const float safeToi = std::max(0.0f, earliestToi - kSkin);
                result.position = result.position + motion * safeToi;

                // 法線方向の velocity 成分を除去 (slide)
                const float vDotN =
                    result.velocity.x * hitNormal.x + result.velocity.y * hitNormal.y + result.velocity.z * hitNormal.z;
                if (vDotN < 0.0f)
                {
                    result.velocity.x -= vDotN * hitNormal.x;
                    result.velocity.y -= vDotN * hitNormal.y;
                    result.velocity.z -= vDotN * hitNormal.z;
                }
                result.contactNormal = hitNormal;

                // 上向き法線 (床、slope ≤ 45°) なら grounded、下方向 velocity をクリップ済
                if (hitNormal.y > 0.7071f)
                    result.grounded = true;
            }
            else
            {
                result.position = result.position + motion;
            }
        }

        // 補助の grounded probe: Capsule 底端から下方向に short ray。床ギリギリで停止した
        // ケースを補足する (slide 後に anyHit が false になり grounded が立たない問題回避)。
        if (!result.grounded)
        {
            const ns::core::Vector3 bottomCenter{
                result.position.x, result.position.y - input.capsuleHalfHeight, result.position.z};
            const DirectX::SimpleMath::Ray ray(bottomCenter, DirectX::SimpleMath::Vector3(0.0f, -1.0f, 0.0f));
            for (const ns::core::AABB& box : input.world)
            {
                float dist = 0.0f;
                if (ray.Intersects(box, dist) && dist <= input.capsuleRadius + kGroundProbeDistance)
                {
                    result.grounded = true;
                    break;
                }
            }
        }

        return result;
    }
} // namespace ns::physics
