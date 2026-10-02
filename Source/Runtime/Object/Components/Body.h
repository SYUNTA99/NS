#pragma once

#include "Runtime/Object/Components/BodyEvents.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Physics/JoltCharacter.h"

#include <memory>

namespace NS::Obj
{
    class CapsuleCollider;
    class HitSensor;
} // namespace NS::Obj

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Obj
{
    //! @brief 登場人物の身体。移動と接地の部品
    //! @details 自機も敵も同じ身体を固定の部品として持ち、自分の状態から呼ぶ。派生させない。
    //! 速度の横縦分解・接地・カプセル寸法の参照・1 フレームの移動だけを持ち、
    //! 能力も調整値も持たない。いつ何を呼ぶかは持ち主の Actor が決める。更新の入口 (OnUpdate) は持たない。
    //! TypeRegistry には登録しない。部品名は持ち主が ForEachPart で付ける。
    //! 衝突の PhysicsScene は使う時に持ち主の Scene から引く。
    //! JoltCharacter だけは作った時の PhysicsScene を持ち続ける。
    //! dt は NS::Platform::FrameTimer::FixedDelta() のみで、DeltaSeconds() は使わない
    //! 依存: NS::Core, NS::Platform::FrameTimer, NS::Phys::JoltCharacter / PhysicsScene, NS::Obj::Scene /
    //! CapsuleCollider
    class Body : public NS::Obj::Component
    {
    public:
        Body() noexcept;

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

        //! 同居する CapsuleCollider の半径。無ければ 0.4。OnStart の前でも同じ答えを返す
        [[nodiscard]] float CapsuleRadius() const noexcept;
        //! 今の当たりの円柱の半分の高さ。球にしていなければ StandingHalfHeight と同じ。
        //! SetSphereShape で球にしている間は 0
        [[nodiscard]] float CapsuleHalfHeight() const noexcept;
        //! 同居する CapsuleCollider の半分の高さ。無ければ 0.5。OnStart の前でも同じ答えを返す。
        //! 球にしている間も変わらないので、立ち姿の寸法はここから引く
        [[nodiscard]] float StandingHalfHeight() const noexcept;

        //! 持ち主の Scene の衝突の PhysicsScene。Scene に居なければ nullptr
        [[nodiscard]] NS::Phys::PhysicsScene* ScenePhysics() const noexcept;

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
        [[nodiscard]] BodyEvents& Events() noexcept { return m_events; }

        //! 同居する CapsuleCollider を控え、静的な当たりの世界から外す
        void OnStart() override;

        // TypeRegistry には登録せず、リフレクションの鎖だけ通す
        NS_REFLECT_NONE(Body, NS::Obj::Component)

        //! @brief 当たりを円柱の長さ 0 のカプセル (半径が同じ球) にするかを切り替える
        //! @details 真の間は CapsuleHalfHeight が 0 を返し、移動と裁定が同じ球で当たる。半径は変えない。
        //! 根の位置は動かさないので、足元を揃えるのは呼び手の仕事
        void SetSphereShape(bool sphere) noexcept;

    private:
        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false; // 直前の Move より前の接地
        NS::Obj::CapsuleCollider* m_capsuleCollider = nullptr;
        std::unique_ptr<NS::Phys::JoltCharacter> m_character;
        BodyEvents m_events;

        //! OnStart で控えた CapsuleCollider。控える前は同居する物を探し、無ければ nullptr
        [[nodiscard]] const NS::Obj::CapsuleCollider* SiblingCapsule() const noexcept;

        //! 体のセンサーの寸法を今の当たりに合わせる。範囲が調べる体と、移動と裁定の当たりを同じ形にする
        void SyncBodySensor() noexcept;

        bool m_sphereShape = false;                 // 当たりを球にしているか。書くのは SetSphereShape だけ
        NS::Obj::HitSensor* m_bodySensor = nullptr; // 同居するカプセルのセンサー。無ければ nullptr
    };
} // namespace NS::Obj
