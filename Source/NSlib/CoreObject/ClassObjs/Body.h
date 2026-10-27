#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/BodyEvents.h"
#include "NSlib/CoreObject/Object.h"

namespace NS
{
    class Collider;
    class Transform;

    //! 速度と接地を保持し、Collider の移動結果を根の姿勢へ反映する部品
    //! 自動更新はしない。持ち主が固定更新の経過秒 dt で各操作を呼ぶ
    //! 速度設定および Move に非有限値を渡すことは不可 (NS_ASSERT)
    class Body : public Object
    {
    public:
        [[nodiscard]] Vector3 Velocity() const noexcept { return m_velocity; }
        void SetVelocity(const Vector3& v) noexcept;

        //! 水平成分のみの速度 (y = 0)。長さが微小な場合は 0 を返す
        [[nodiscard]] Vector3 LateralVelocity() const noexcept;
        //! 水平成分のみを差し替える。引数の y は無視する
        void SetLateralVelocity(const Vector3& v) noexcept;

        [[nodiscard]] float VerticalVelocity() const noexcept { return m_velocity.y; }
        void SetVerticalVelocity(float y) noexcept;

        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        //! 直前の Move より前の接地状態
        [[nodiscard]] bool WasGrounded() const noexcept { return m_wasGrounded; }
        //! Move を通さず位置を直接変更する際、現在と直前の接地状態を同時に設定する
        void SetGrounded(bool grounded) noexcept;

        //! 直前の Move で進行方向を地形の面に遮られたか (面に沿う移動は含まない)
        [[nodiscard]] bool IsBlocked() const noexcept { return m_isBlocked; }
        //! 直前の Move で遮った面のうち、速度に最も対向する面の外向き単位法線。未遮断時は 0
        [[nodiscard]] Vector3 BlockNormal() const noexcept { return m_blockNormal; }

        //! 負の値で下向きに加速する。非有限値または dt <= 0 なら変更しない
        void Gravity(float gravity, float dt) noexcept;

        //! 同じアクターの Collider で root を動かし、位置・速度・接地を更新する (両方 nullptr 不可)
        //! シーン未所属時は衝突判定と接地通知を行わない。dt は秒、maxStepHeight は乗り越え可能な段差高 (m)
        void Move(Transform* root, Collider* collider, float dt, float maxStepHeight) noexcept;

        [[nodiscard]] BodyEvents& Events() noexcept { return m_events; }

        NS_REFLECT_NONE(Body, Object)

    private:
        Vector3 m_velocity{0.0f, 0.0f, 0.0f}; // m/s
        bool m_isGrounded = false;
        bool m_wasGrounded = false;
        bool m_isBlocked = false;
        Vector3 m_blockNormal{0.0f, 0.0f, 0.0f};
        BodyEvents m_events;
    };
} // namespace NS
