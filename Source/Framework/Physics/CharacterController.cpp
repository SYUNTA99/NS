#include "Framework/Physics/CharacterController.h"

#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

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
} // namespace

namespace NS::Physics
{
    CharacterControllerResult CharacterController::Update(const CharacterControllerInput& input) noexcept
    {
        NS_SCOPED_TIMER(::NS::Core::LogCat::Physics, "CharacterController::Update");

        CharacterControllerResult result;
        result.position = input.position;
        result.velocity = input.velocity;
        result.grounded = false;
        result.contactNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};

        if (!std::isfinite(input.dt) || input.dt <= 0.0f || !std::isfinite(input.capsuleRadius) ||
            input.capsuleRadius < 0.0f || !std::isfinite(input.capsuleHalfHeight) || input.capsuleHalfHeight < 0.0f)
            return result;

        const float subDt = input.dt / static_cast<float>(kMaxSubSteps);

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

                if (input.physicsWorld != nullptr)
                {
                    const SweepHit hit = input.physicsWorld->SweepCapsule(cap, motion);
                    earliestToi = hit.toi;
                    hitNormal = hit.normal;
                    anyHit = hit.hit;
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

            if (input.physicsWorld != nullptr)
                result.grounded =
                    input.physicsWorld->ProbeGround(bottomCenter, input.capsuleRadius + kGroundProbeDistance);
        }

        return result;
    }
} // namespace NS::Physics
