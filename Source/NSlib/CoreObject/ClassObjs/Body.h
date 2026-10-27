#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/BodyEvents.h"
#include "NSlib/CoreObject/Object.h"

namespace NS
{
    class Collider;
    class Transform;

    //! @brief 速度と接地を保持し、Collider の移動結果を根の姿勢へ反映する部品
    //! @details 自動更新はしない。持ち主が固定更新の経過秒 dt で各操作を呼ぶ
    class Body : public Object
    {
    public:
        [[nodiscard]] Vector3 Velocity() const noexcept { return m_velocity; }
        void SetVelocity(const Vector3& v) noexcept { m_velocity = v; }

        //! 水平成分だけを取り出した速度で y は 0。長さが微小 (IsNearlyZeroHorizontal) なら 0 を返す
        [[nodiscard]] Vector3 LateralVelocity() const noexcept;
        //! 水平成分だけを差し替える。引数の y は読まない
        void SetLateralVelocity(const Vector3& v) noexcept;

        [[nodiscard]] float VerticalVelocity() const noexcept { return m_velocity.y; }
        void SetVerticalVelocity(float y) noexcept { m_velocity.y = y; }

        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        //! 直前の Move より前の接地。着地したフレームを見分けるのに使う
        [[nodiscard]] bool WasGrounded() const noexcept { return m_wasGrounded; }
        //! 掴まりのように移動を通さず位置を直に置く時、接地の控えも合わせて置く
        void SetGrounded(bool grounded) noexcept;

        //! 直前の Move で、速度の向きへ進むのを地形の面が遮ったか。面に沿う動きは遮ったに数えない
        [[nodiscard]] bool IsBlocked() const noexcept { return m_isBlocked; }
        //! 直前の Move で遮った面のうち、速度に一番向かい合った面の外向きの単位法線。遮らなかったら 0
        [[nodiscard]] Vector3 BlockNormal() const noexcept { return m_blockNormal; }

        //! 水平の単位方向へ加速し、横滑りを減らす。上限を超える順方向速度は保つ
        //! turningDrag・acceleration は m/s²、topSpeed は m/s、dt は秒
        void Accelerate(
            const Vector3& direction, float turningDrag, float acceleration, float topSpeed, float dt) noexcept;

        //! 水平の速さを 1 フレームに deceleration × dt ずつ減らし、ちょうど 0 で止める。縦は触らない
        void Decelerate(float deceleration, float dt) noexcept;

        //! 重力の値は呼び手が選ぶ。負の値で重力方向へ加速する。非有限値または dt が 0 以下なら変更しない
        void Gravity(float gravity, float dt) noexcept;

        //! 同じアクターの Collider で root を動かし、root の位置・速度・接地を更新する
        //! シーンなしでは衝突と接地通知を行わない。dt は秒、maxStepHeight は段差の高さ m
        void Move(Transform* root, Collider* collider, float dt, float maxStepHeight) noexcept;

        //! 接地の通知の受け口。購読は後から足せる
        [[nodiscard]] BodyEvents& Events() noexcept { return m_events; }

        // TypeRegistry には登録しない。欄は持たず、古い場面に残る寸法の鍵は読み飛ばす。寸法の欄は Collider が持つ
        NS_REFLECT_NONE(Body, Object)

    private:
        Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false; // 直前の Move より前の接地
        bool m_isBlocked = false;
        Vector3 m_blockNormal{0.0f, 0.0f, 0.0f};
        BodyEvents m_events;
    };
} // namespace NS
