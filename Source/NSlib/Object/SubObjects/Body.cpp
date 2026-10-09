#include "NSlib/Object/SubObjects/Body.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/Gravity.h"
#include "NSlib/Object/Transform.h"

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

namespace NS::Obj
{
    NS::Vector3 Body::LateralVelocity() const noexcept
    {
        // 8 m/s を減速度 40 で止めると、最後のフレームに 6×10⁻⁷ m/s の端数が残る
        // 0 と読まないと、止まったかの判定が 1 フレーム遅れる
        if (m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z < NS::k_Epsilon * NS::k_Epsilon)
        {
            return NS::Vector3{0.0f, 0.0f, 0.0f};
        }
        return NS::Vector3{m_velocity.x, 0.0f, m_velocity.z};
    }

    void Body::SetLateralVelocity(const NS::Vector3& v) noexcept
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
        const NS::Vector3& direction, float turningDrag, float acceleration, float topSpeed, float dt) noexcept
    {
        const NS::Vector3 lateral = LateralVelocity();
        float speed = direction.x * lateral.x + direction.z * lateral.z;
        float turningX = lateral.x - direction.x * speed;
        float turningZ = lateral.z - direction.z * speed;

        const float lateralSpeed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
        if (lateralSpeed < topSpeed || speed < 0.0f)
        {
            speed = NS::Clamp(speed + acceleration * dt, -topSpeed, topSpeed);
        }

        ShrinkHorizontal(turningX, turningZ, turningDrag * dt);
        m_velocity.x = direction.x * speed + turningX;
        m_velocity.z = direction.z * speed + turningZ;
    }

    void Body::Decelerate(float deceleration, float dt) noexcept
    {
        NS::Vector3 lateral = LateralVelocity();
        ShrinkHorizontal(lateral.x, lateral.z, deceleration * dt);
        m_velocity.x = lateral.x;
        m_velocity.z = lateral.z;
    }

    void Body::Gravity(float gravity, float dt) noexcept
    {
        const NS::Obj::Actor* owner = Owner();
        if (owner == nullptr)
        {
            m_velocity.y += gravity * dt;
            return;
        }
        NS::Obj::AddGravity(*owner, m_velocity, -gravity, dt);
    }

    // 天井の当たりは持たない。JoltCharacter が接触面へ速度を射影するので、
    // 天井に当たったフレームの上向き速度は Move を抜けた時点で 0 になっている
    void Body::Move(NS::Obj::Collider& collider, float dt, float maxStepHeight) noexcept
    {
        const NS::Obj::ColliderMove moved = collider.Move(RootTransform().Position(), m_velocity, dt, maxStepHeight);
        RootTransform().SetPosition(moved.position);
        m_velocity = moved.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = moved.grounded;
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
} // namespace NS::Obj
