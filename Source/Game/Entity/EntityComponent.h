#pragma once

#include "Game/Entity/EntityEvents.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Physics/JoltCharacter.h"

#include <memory>

namespace NS::Object
{
    class CapsuleColliderComponent;
}

namespace NS::Physics
{
    class PhysicsWorld;
}

namespace NS::Game::Entity
{
    //! @brief 登場人物に共通する移動と接地の抽象基底
    //! @details 敵も自機もここから派生する。速度の横縦分解・接地・カプセル寸法の参照・1 歩の移動だけを持ち、
    //! 能力も調整値も持たない
    //! 状態機械は派生が具象の型で持つ。1 歩の中身は HandleStates の中で派生が並べる
    //! TypeRegistry には登録しない。実体化できるのは派生だけ
    //! 衝突 world は使う時に持ち主の Scene から引く。JoltCharacter だけは作った時の world を持ち続ける
    //! dt は NS::Core::FrameTimer::FixedDelta() のみで、DeltaSeconds() は使わない
    //! 依存: NS::Core, NS::Physics::JoltCharacter / PhysicsWorld, NS::Object::Scene / CapsuleColliderComponent
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

        //! 同居する CapsuleColliderComponent の半径。無ければ 0.4
        [[nodiscard]] float CapsuleRadius() const noexcept;
        //! 同居する CapsuleColliderComponent の半分の高さ。無ければ 0.5
        [[nodiscard]] float CapsuleHalfHeight() const noexcept;

        //! 持ち主の Scene の衝突 world。Scene に居なければ nullptr
        [[nodiscard]] NS::Physics::PhysicsWorld* PhysicsWorld() const noexcept;

        //! 水平の目標速度へ一次遅れで近づける。縦は触らない
        void Accelerate(const NS::Core::Vector3& targetHorizontal, float tau, float dt) noexcept;

        //! 水平を 0 へ一次遅れで近づける
        void Decelerate(float tau, float dt) noexcept;

        //! 縦速度へ重力を 1 歩ぶん当てる。値の選び分け (上昇 / 下降 / 頂点) は派生の仕事
        void Gravity(float gravity, float dt) noexcept;

        //! JoltCharacter へ 1 歩渡し、位置・速度・接地を更新する
        //! 持ち主が Scene に居なければ当たりを見ずに速度ぶん進め、接地は false にする
        void Move(float dt) noexcept;

        //! 接地の通知の受け口。購読は後から足せる
        [[nodiscard]] EntityEvents& Events() noexcept { return m_events; }

        //! 同居する CapsuleColliderComponent を控え、静的な当たりの世界から外す
        void OnStart() override;
        //! 1 歩ぶん HandleStates を呼ぶ
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
        bool m_wasGrounded = false; // 直前の Move より前の接地
        NS::Object::CapsuleColliderComponent* m_capsuleCollider = nullptr;
        std::unique_ptr<NS::Physics::JoltCharacter> m_character;
        EntityEvents m_events;
    };
} // namespace NS::Game::Entity
