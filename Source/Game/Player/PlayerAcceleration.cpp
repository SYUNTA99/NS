#include "Game/Player/PlayerAcceleration.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

#include <cmath>

namespace
{
    // 水平の長さを drop だけ縮めた水平成分。drop 以下なら 0
    NS::Vector3 ShrinkHorizontal(const NS::Vector3& v, float drop) noexcept
    {
        const float length = NS::HorizontalLength(v);
        if (length <= drop || length == 0.0f)
        {
            return NS::Vector3{};
        }
        return NS::HorizontalPart(v) * ((length - drop) / length);
    }
} // namespace

namespace GL
{
    namespace Player
    {
        void Accelerate(
            NS::Body* body, const NS::Vector3& direction, float turningDrag, float acceleration, float topSpeed,
            float dt) noexcept
        {
            NS_ASSERT(Game, body != nullptr, "Accelerate: 体が空");
            const NS::Vector3 lateral = body->LateralVelocity();
            float speed = NS::DotHorizontal(direction, lateral);
            const NS::Vector3 turning = NS::HorizontalPart(lateral - direction * speed);

            const float lateralSpeed = NS::HorizontalLength(lateral);
            if (lateralSpeed < topSpeed || speed < 0.0f)
            {
                speed = NS::Clamp(speed + acceleration * dt, -topSpeed, topSpeed);
            }

            body->SetLateralVelocity(
                NS::HorizontalPart(direction * speed) + ShrinkHorizontal(turning, turningDrag * dt));
        }

        void Decelerate(NS::Body* body, float deceleration, float dt) noexcept
        {
            NS_ASSERT(Game, body != nullptr, "Decelerate: 体が空");
            body->SetLateralVelocity(ShrinkHorizontal(body->LateralVelocity(), deceleration * dt));
        }
    } // namespace Player
} // namespace GL
