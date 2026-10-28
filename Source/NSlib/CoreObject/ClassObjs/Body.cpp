#include "NSlib/CoreObject/ClassObjs/Body.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/Phys/PhysicsScene.h"

namespace NS
{
    Vector3 Body::LateralVelocity() const noexcept
    {
        // 8 m/s を減速度 40 で止めると、最後のフレームに 6×10⁻⁷ m/s の端数が残る
        // 0 と読まないと、止まったかの判定が 1 フレーム遅れる
        if (IsNearlyZeroHorizontal(m_velocity))
        {
            return Vector3{0.0f, 0.0f, 0.0f};
        }
        return Vector3{m_velocity.x, 0.0f, m_velocity.z};
    }

    void Body::SetVelocity(const Vector3& v) noexcept
    {
        NS_ASSERT(Scene, IsFinite(v), "Body::SetVelocity: 速度が有限でない");
        m_velocity = v;
    }

    void Body::SetLateralVelocity(const Vector3& v) noexcept
    {
        NS_ASSERT(Scene, IsFinite(HorizontalPart(v)), "Body::SetLateralVelocity: 水平の速度が有限でない");
        m_velocity.x = v.x;
        m_velocity.z = v.z;
    }

    void Body::SetVerticalVelocity(float y) noexcept
    {
        NS_ASSERT(Scene, std::isfinite(y), "Body::SetVerticalVelocity: 縦の速度が有限でない");
        m_velocity.y = y;
    }

    void Body::SetGrounded(bool grounded) noexcept
    {
        m_wasGrounded = grounded;
        m_isGrounded = grounded;
    }

    void Body::Gravity(float gravity, float dt) noexcept
    {
        // 非有限の重力と経過秒は AddGravity が捨てる。有限どうしの掛け算の溢れはここでは見ない
        // 重力の大きさの範囲は、重力を調整値として持つ側が受け持つ
        Phys::AddGravity(m_velocity, -gravity, dt);
    }

    // 天井の当たりは持たない。JoltCharacter が接触面へ速度を射影するので、
    // 天井に当たったフレームの上向き速度は Move を抜けた時点で 0 になっている
    void Body::Move(Transform* root, Collider* collider, float dt, float maxStepHeight, float maxSlopeRadians) noexcept
    {
        NS_ASSERT(Scene, collider != nullptr, "Body::Move: 当たりの部品が空");
        NS_ASSERT(Scene, root != nullptr, "Body::Move: 動かすルートが空");
        const ColliderMove moved = collider->Move(root->Position(), m_velocity, dt, maxStepHeight, maxSlopeRadians);
        NS_ASSERT(Scene, IsFinite(moved.position), "Body::Move: 当たりの部品が返した位置が有限でない");
        NS_ASSERT(Scene, IsFinite(moved.velocity), "Body::Move: 当たりの部品が返した速度が有限でない");
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
