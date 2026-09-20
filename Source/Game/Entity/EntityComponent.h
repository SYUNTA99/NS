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
    class PhysicsScene;
}

namespace NS::Game::Entity
{
    //! @brief 登場人物に共通する移動と接地の抽象基底
    //! @details 敵も自機もここから派生する。速度の横縦分解・接地・カプセル寸法の参照・1 フレームの移動だけを持ち、
    //! 能力も調整値も持たない。状態機械は派生が具象の型で持つ。
    //! 1 フレームは HandleStates で派生が能力を並べ、続く HandleMovement で 1 回動かす。
    //! TypeRegistry には登録しない。実体化できるのは派生だけ。
    //! 衝突の PhysicsScene は使う時に持ち主の Scene から引く。
    //! JoltCharacter だけは作った時の PhysicsScene を持ち続ける。
    //! dt は NS::Core::FrameTimer::FixedDelta() のみで、DeltaSeconds() は使わない
    //! 依存: NS::Core, NS::Physics::JoltCharacter / PhysicsScene, NS::Object::Scene / CapsuleColliderComponent
    class EntityComponent : public NS::Object::Component
    {
    public:
        EntityComponent() noexcept;

        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept { return m_velocity; }
        void SetVelocity(const NS::Core::Vector3& v) noexcept { m_velocity = v; }

        //! 水平成分だけを取り出した速度で y は 0。長さが k_Epsilon 未満なら 0 を返す
        [[nodiscard]] NS::Core::Vector3 LateralVelocity() const noexcept;
        //! 水平成分だけを差し替える。引数の y は読まない
        void SetLateralVelocity(const NS::Core::Vector3& v) noexcept;

        [[nodiscard]] float VerticalVelocity() const noexcept { return m_velocity.y; }
        void SetVerticalVelocity(float y) noexcept { m_velocity.y = y; }

        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        //! 直前の Move より前の接地。着地したフレームを見分けるのに使う
        [[nodiscard]] bool WasGrounded() const noexcept { return m_wasGrounded; }
        //! 掴まりのように移動を通さず位置を直に置く時、接地の控えも合わせて置く
        void SetGrounded(bool grounded) noexcept;

        //! 同居する CapsuleColliderComponent の半径。無ければ 0.4
        [[nodiscard]] float CapsuleRadius() const noexcept;
        //! 同居する CapsuleColliderComponent の半分の高さ。無ければ 0.5
        [[nodiscard]] float CapsuleHalfHeight() const noexcept;

        //! 持ち主の Scene の衝突の PhysicsScene。Scene に居なければ nullptr
        [[nodiscard]] NS::Physics::PhysicsScene* ScenePhysics() const noexcept;

        //! @brief 水平の速度を direction へ加速し、向きからずれた成分を turningDrag で減らす。縦は触らない
        //! @details 向きの成分に acceleration × dt を足すのは、水平の速さが topSpeed
        //! 未満か、向きの成分が逆向きの時だけ。足した後は ±topSpeed で切る
        //! 水平の速さが topSpeed 以上で向きへ進んでいる時は足しも削りもしない
        //! @param[in] direction 加速する向き。水平の単位ベクトル
        //! @param[in] turningDrag ずれた成分を減らす減速度 (m/s²)
        //! @param[in] acceleration 向きの成分へ足す加速度 (m/s²)
        //! @param[in] topSpeed 向きの成分を加速で上げる上限 (m/s)
        //! @param[in] dt 1 フレームの秒数
        void Accelerate(const NS::Core::Vector3& direction,
                        float turningDrag,
                        float acceleration,
                        float topSpeed,
                        float dt) noexcept;

        //! 水平の速さを 1 フレームに deceleration × dt ずつ減らし、ちょうど 0 で止める。縦は触らない
        void Decelerate(float deceleration, float dt) noexcept;

        //! 縦速度へ重力を 1 フレームぶん当てる。値の選び分け (上昇 / 下降 / 頂点) は派生の仕事
        void Gravity(float gravity, float dt) noexcept;

        //! JoltCharacter を 1 フレーム進め、位置・速度・接地を更新する
        //! 持ち主が Scene に居なければ当たりを見ずに速度ぶん進め、接地は false にする
        //! maxStepHeight は走ったまま登れる段の高さ (m) で、JoltCharacter::Step へそのまま渡す
        void Move(float dt, float maxStepHeight) noexcept;

        //! 接地の通知の受け口。購読は後から足せる
        [[nodiscard]] EntityEvents& Events() noexcept { return m_events; }

        //! 同居する CapsuleColliderComponent を控え、静的な当たりの世界から外す
        void OnStart() override;
        //! 1 フレームぶん HandleStates を呼び、続けて HandleMovement で動かす
        void OnUpdate() override;

        // 抽象基底なので TypeRegistry には登録せず、リフレクションの鎖だけ通す
        NS_REFLECT_NONE(EntityComponent, NS::Object::Component)

    protected:
        //! 1 フレームの中身。派生が状態機械を回すか能力を直に並べる
        virtual void HandleStates(float dt) = 0;
        //! @brief 状態が決めた速度で 1 フレーム動かす。既定は段差を登らずに進む
        //! @details Move を呼ぶのは 1 フレームにここだけ。状態の側で動かすと、状態を足した時に呼び忘れても
        //! ビルドが通り、その状態の間だけ動かなくなる
        virtual void HandleMovement(float dt) noexcept;
        //! 稼働していないフレームでも 1 フレーム限りの入力を派生が落とせるようにする。既定は何もしない
        virtual void OnStepSkipped() {}

        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false; // 直前の Move より前の接地
        NS::Object::CapsuleColliderComponent* m_capsuleCollider = nullptr;
        std::unique_ptr<NS::Physics::JoltCharacter> m_character;
        EntityEvents m_events;
    };
} // namespace NS::Game::Entity
