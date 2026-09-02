#include "Runtime/Physics/CapsuleMover.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>

namespace
{
    constexpr int k_MaxSubSteps = 4;
    // 衝突時刻の手前で止める割合。距離でなく 0〜1 の toi から引くため、隙間は移動量に比例して最高速で 0.7mm
    // 低速では隙間がほぼ 0 になる。壁への貼り付きを防いでいるのは ComputePushOut の 1mm 側
    constexpr float k_Skin = 0.01f;
    // 着地直後に player.y が跳ねて grounded がちらつくのを抑えるため raycast をこの分だけ下へ延ばす
    constexpr float k_GroundProbeDistance = 0.2f;
    // 歩ける床とみなす normal.y の閾値。cos 45 ≈ 0.707、45° 含むため 0.7
    constexpr float k_FloorNormalY = 0.7f;
} // namespace

namespace NS::Physics
{

    CapsuleMoverResult CapsuleMover::Update(const CapsuleMoverInput& input) noexcept
    {
        NS_SCOPED_TIMER(Physics, "CapsuleMover::Update");

        CapsuleMoverResult result;
        result.position = input.position;
        result.velocity = input.velocity;
        result.grounded = false;
        result.contactNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        if (!std::isfinite(input.dt) || input.dt <= 0.0f || !std::isfinite(input.capsuleRadius) ||
            input.capsuleRadius < 0.0f || !std::isfinite(input.capsuleHalfHeight) || input.capsuleHalfHeight < 0.0f)
            return result;

        // 掃引は重なった相手に toi 0 で当たり続け、埋まったままでは動けない。動く前に重なりから押し出す
        if (input.physicsWorld != nullptr)
        {
            Capsule startCap;
            startCap.center = result.position;
            startCap.axis = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
            startCap.halfHeight = input.capsuleHalfHeight;
            startCap.radius = input.capsuleRadius;
            result.position = result.position + input.physicsWorld->ComputePushOut(startCap);
        }

        const float subDt = input.dt / static_cast<float>(k_MaxSubSteps);

        for (int step = 0; step < k_MaxSubSteps; ++step)
        {
            // 床に接触したまま壁へ走り込んだ時も壁の接触を取りこぼさないよう、 残り motion を最大 k_MaxSlideIters
            // 回まで再 swept する
            constexpr int k_MaxSlideIters = 4;
            float remainingTime = 1.0f; // この substep のうち未消費の比率で 0..1

            for (int slideIter = 0; slideIter < k_MaxSlideIters && remainingTime > 0.0f; ++slideIter)
            {
                NS::Core::Vector3 motion = result.velocity * (subDt * remainingTime);

                if (std::abs(motion.x) < 1e-9f && std::abs(motion.y) < 1e-9f && std::abs(motion.z) < 1e-9f)
                    break;

                Capsule cap;
                cap.center = result.position;
                cap.axis = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
                cap.halfHeight = input.capsuleHalfHeight;
                cap.radius = input.capsuleRadius;

                float earliestToi = 1.0f;
                NS::Core::Vector3 hitNormal{0.0f, 0.0f, 0.0f};
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

                const float safeToi = std::max(0.0f, earliestToi - k_Skin);
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

                if (hitNormal.y > k_FloorNormalY)
                    result.grounded = true;

                remainingTime *= std::max(0.0f, 1.0f - earliestToi);
            }
        }

        // slide を終えても grounded が立たない床ギリギリの場合を、 Capsule 底端から下向きの ray で拾う
        if (!result.grounded)
        {
            const NS::Core::Vector3 bottomCenter{
                result.position.x, result.position.y - input.capsuleHalfHeight, result.position.z};

            if (input.physicsWorld != nullptr)
                result.grounded =
                    input.physicsWorld->ProbeGround(bottomCenter, input.capsuleRadius + k_GroundProbeDistance);
        }

        return result;
    }
} // namespace NS::Physics
