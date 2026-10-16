#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/CoreObject/SubObjects/BodyEvents.h"

namespace NS
{
    class Collider;

    //! @brief 速度と接地を保持し、Collider の移動結果を根の姿勢へ反映する部品
    //! @details 自動更新はしない。持ち主が固定更新の経過秒 dt で各操作を呼ぶ
    class Body : public SubObject
    {
    public:
        explicit Body(SceneActor* owner) noexcept : m_owner(owner) {}

        [[nodiscard]] Vector3 Velocity() const noexcept { return m_velocity; }
        void SetVelocity(const Vector3& v) noexcept { m_velocity = v; }

        //! 水平成分だけを取り出した速度で y は 0。長さが k_Epsilon 未満なら 0 を返す
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

        //! 水平の単位方向へ加速し、横滑りを減らす。上限を超える順方向速度は保つ
        //! turningDrag・acceleration は m/s²、topSpeed は m/s、dt は秒
        void Accelerate(
            const Vector3& direction, float turningDrag, float acceleration, float topSpeed, float dt) noexcept;

        //! 水平の速さを 1 フレームに deceleration × dt ずつ減らし、ちょうど 0 で止める。縦は触らない
        void Decelerate(float deceleration, float dt) noexcept;

        //! 重力の値は呼び手が選ぶ。負の値でシーンの重力方向へ加速する
        void Gravity(float gravity, float dt) noexcept;

        //! 同じ個体の Collider で動かし、位置・速度・接地を更新する
        //! シーンなしでは衝突と接地通知を行わない。dt は秒、maxStepHeight は段差の高さ m
        void Move(Collider& collider, float dt, float maxStepHeight) noexcept;

        //! 接地の通知の受け口。購読は後から足せる
        [[nodiscard]] BodyEvents& Events() noexcept { return m_events; }

        // TypeRegistry には登録しない。欄は持たず、古い場面に残る寸法の鍵は読み飛ばす。寸法の欄は Collider が持つ
        NS_REFLECT_NONE(Body, SubObject)

    private:
        Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false; // 直前の Move より前の接地
        BodyEvents m_events;
        SceneActor* m_owner = nullptr; // 持ち主 (非所有)
    };
} // namespace NS
