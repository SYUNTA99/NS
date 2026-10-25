#include "NSlib/CoreObject/ClassObjs/Body.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/Phys/PhysicsScene.h"

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

namespace NS
{
    Vector3 Body::LateralVelocity() const noexcept
    {
        // 8 m/s を減速度 40 で止めると、最後のフレームに 6×10⁻⁷ m/s の端数が残る
        // 0 と読まないと、止まったかの判定が 1 フレーム遅れる
        if (m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z < k_Epsilon * k_Epsilon)
        {
            return Vector3{0.0f, 0.0f, 0.0f};
        }
        return Vector3{m_velocity.x, 0.0f, m_velocity.z};
    }

    void Body::SetLateralVelocity(const Vector3& v) noexcept
    {
        m_velocity.x = v.x;
        m_velocity.z = v.z;
    }

    void Body::SetGrounded(bool grounded) noexcept
    {
        m_wasGrounded = grounded;
        m_isGrounded = grounded;
    }

    void Body::Accelerate(
        const Vector3& direction, float turningDrag, float acceleration, float topSpeed, float dt) noexcept
    {
        const Vector3 lateral = LateralVelocity();
        float speed = direction.x * lateral.x + direction.z * lateral.z;
        float turningX = lateral.x - direction.x * speed;
        float turningZ = lateral.z - direction.z * speed;

        const float lateralSpeed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
        if (lateralSpeed < topSpeed || speed < 0.0f)
        {
            speed = Clamp(speed + acceleration * dt, -topSpeed, topSpeed);
        }

        ShrinkHorizontal(turningX, turningZ, turningDrag * dt);
        m_velocity.x = direction.x * speed + turningX;
        m_velocity.z = direction.z * speed + turningZ;
    }

    void Body::Decelerate(float deceleration, float dt) noexcept
    {
        Vector3 lateral = LateralVelocity();
        ShrinkHorizontal(lateral.x, lateral.z, deceleration * dt);
        m_velocity.x = lateral.x;
        m_velocity.z = lateral.z;
    }

    void Body::Gravity(float gravity, float dt) noexcept
    {
        Phys::AddGravity(m_velocity, -gravity, dt);
    }

    // 天井の当たりは持たない。JoltCharacter が接触面へ速度を射影するので、
    // 天井に当たったフレームの上向き速度は Move を抜けた時点で 0 になっている
    void Body::Move(Transform* root, Collider* collider, float dt, float maxStepHeight) noexcept
    {
        NS_ASSERT(Scene, collider != nullptr, "Body::Move: 当たりの部品が空");
        NS_ASSERT(Scene, root != nullptr, "Body::Move: 動かすルートが空");
        const ColliderMove moved = collider->Move(root->Position(), m_velocity, dt, maxStepHeight);
        root->SetPosition(moved.position);
        m_velocity = moved.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = moved.grounded;
        m_isBlocked = moved.blocked;
        m_blockNormal = Vector3{0.0f, 0.0f, 0.0f};
        if (moved.blocked)
        {
            m_blockNormal = moved.blockNormal;
        }
        // Scene に居ない間は知らせを出さない
        if (!moved.inWorld)
        {
            return;
        }

        // 発火は位置・速度・接地を書き終えた後。途中で呼ぶと購読側がそのフレームだけ古い値を読む
        if (!m_wasGrounded && m_isGrounded)
        {
            m_events.onGroundEnter.Invoke();
        }
        else if (m_wasGrounded && !m_isGrounded)
        {
            m_events.onGroundExit.Invoke();
        }
    }
} // namespace NS
