#include "Game/Player/PlayerAcceleration.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

#include <cmath>

namespace
{
    // 水平の長さを drop だけ縮め、drop 以下なら 0 にする
    void ShrinkHorizontal(float& x, float& z, float drop) noexcept
    {
        const float length = std::sqrt(x * x + z * z);
        if (length <= drop || length == 0.0f)
        {
            x = 0.0f;
            z = 0.0f;
            return;
        }
        const float scale = (length - drop) / length;
        x *= scale;
        z *= scale;
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
            float turningX = lateral.x - direction.x * speed;
            float turningZ = lateral.z - direction.z * speed;

            const float lateralSpeed = NS::HorizontalLength(lateral);
            if (lateralSpeed < topSpeed || speed < 0.0f)
            {
                speed = NS::Clamp(speed + acceleration * dt, -topSpeed, topSpeed);
            }

            ShrinkHorizontal(turningX, turningZ, turningDrag * dt);
            body->SetLateralVelocity(
                NS::Vector3{direction.x * speed + turningX, 0.0f, direction.z * speed + turningZ});
        }

        void Decelerate(NS::Body* body, float deceleration, float dt) noexcept
        {
            NS_ASSERT(Game, body != nullptr, "Decelerate: 体が空");
            NS::Vector3 lateral = body->LateralVelocity();
            ShrinkHorizontal(lateral.x, lateral.z, deceleration * dt);
            body->SetLateralVelocity(lateral);
        }
    } // namespace Player
} // namespace GL
