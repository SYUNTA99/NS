#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Physics/CapsuleMover.h"

namespace NS::Object
{
    class CapsuleColliderComponent;
}

namespace NS::Game::Entity
{
    //! @brief 登場人物に共通する移動と接地の抽象基底
    //! @details 敵も自機もここから派生する。速度の横縦分解・接地・カプセル寸法・1 歩の移動だけを持ち、
    //! 能力も調整値も持たない
    //! 状態機械は派生が具象の型で持つ。1 歩の中身は HandleStates の中で派生が並べる
    //! TypeRegistry には登録しない。実体化できるのは派生だけ
    //! 衝突 query 元は OnStart で所属 scene から非所有で借りる
    //! dt は NS::Core::FrameTimer::FixedDelta() のみで、DeltaSeconds() は使わない
    //! 依存: NS::Core, NS::Physics::CapsuleMover / PhysicsWorld, NS::Object::CapsuleColliderComponent
    class EntityComponent : public NS::Object::Component
    {
    public:
        EntityComponent() noexcept;

        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept { return m_velocity; }
        void SetVelocity(const NS::Core::Vector3& v) noexcept { m_velocity = v; }

        //! 水平成分だけを取り出した速度で y は 0
        [[nodiscard]] NS::Core::Vector3 LateralVelocity() const noexcept;
        //! 水平成分だけを差し替える。引数の y は読まない
        void SetLateralVelocity(const NS::Core::Vector3& v) noexcept;

        [[nodiscard]] float VerticalVelocity() const noexcept { return m_velocity.y; }
        void SetVerticalVelocity(float y) noexcept { m_velocity.y = y; }

        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        //! 直前の Move より前の接地。着地した歩を見分けるのに使う
        [[nodiscard]] bool WasGrounded() const noexcept { return m_wasGrounded; }
        //! 掴まりのように移動を通さず位置を直に置く時、接地の控えも合わせて置く
        void SetGrounded(bool grounded) noexcept;

        //! カプセル半径を設定する。0.001 未満は丸める。負のまま渡すと CapsuleMover が 1 歩ぶん動かさずに返す
        void SetCapsuleRadius(float r) noexcept
        {
            if (r < 0.001f)
                m_capsuleRadius = 0.001f;
            else
                m_capsuleRadius = r;
        }
        //! カプセル半分の高さを設定する。下限は半径と同じ 0.001 で、理由も同じ
        void SetCapsuleHalfHeight(float h) noexcept
        {
            if (h < 0.001f)
                m_capsuleHalfHeight = 0.001f;
            else
                m_capsuleHalfHeight = h;
        }
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_capsuleRadius; }
        [[nodiscard]] float CapsuleHalfHeight() const noexcept { return m_capsuleHalfHeight; }

        //! 衝突 query 元を非所有で借りる。シーン無しで動かす検証台の継ぎ目で、
        //! 本編は OnStart が所属 scene の world を取る
        void SetPhysicsWorld(const NS::Physics::PhysicsWorld* world) noexcept { m_world = world; }
        [[nodiscard]] const NS::Physics::PhysicsWorld* PhysicsWorld() const noexcept { return m_world; }

        //! CapsuleMover へ 1 歩渡し、位置・速度・接地を更新する
        void Move(float dt) noexcept;

        //! 未注入なら所属 scene の衝突 world を借り、同居する CapsuleColliderComponent を控える
        void OnStart() override;
        //! カプセル寸法を写してから 1 歩ぶん HandleStates を呼ぶ
        void OnUpdate() override;

        // 抽象基底なので TypeRegistry には登録せず、リフレクションの鎖だけ通す
        NS_REFLECT_NONE(EntityComponent, NS::Object::Component)

    protected:
        //! 1 歩の中身。派生が状態機械を回すか能力を直に並べる
        virtual void HandleStates(float dt) = 0;
        //! 稼働していない歩でも 1 歩限りの入力を派生が落とせるようにする。既定は何もしない
        virtual void OnStepSkipped() {}

        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false;                         // 直前の Move より前の接地
        float m_capsuleRadius = 0.4f;                       // カプセル半径
        float m_capsuleHalfHeight = 0.5f;                   // カプセル半分の高さ
        const NS::Physics::PhysicsWorld* m_world = nullptr; // 衝突判定に使う physics world (非所有)
        NS::Object::CapsuleColliderComponent* m_capsuleCollider = nullptr;
        NS::Physics::CapsuleMover m_controller; // 数値計算を任せる controller
    };
} // namespace NS::Game::Entity
