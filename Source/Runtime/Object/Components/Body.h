#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/BodyEvents.h"

namespace NS::Obj
{
    class Collider;

    //! @brief 登場人物の身体。速度と接地の部品
    //! @details 自機も敵も同じ身体を固定の部品として持ち、自分の状態から呼ぶ。派生させない。
    //! 速度の横縦分解・接地・接地の知らせだけを持ち、能力も調整値も当たりの寸法も持たない。
    //! いつ何を呼ぶかは持ち主の Actor が決める。更新の入口 (OnUpdate) は持たない。
    //! 地形に当てて押し返す移動は同じ Actor の Collider に頼み、その結果で根の位置・速度・接地を書く。
    //! TypeRegistry には登録しない。部品名は持ち主が ForEachPart で付ける。
    //! dt は呼び手が引数で渡す。呼び手は固定ステップの秒を渡し、描画フレームの秒は渡さない
    //! 依存: NS::Core, NS::Obj::Actor / Collider
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

        //! 縦速度へ重力を 1 フレームぶん当てる。上り・下り・頂点の値を選ぶのは持ち主の Player
        //! (PlayerParams::Gravity / ReboundGravity と ChooseGravity)
        void Gravity(float gravity, float dt) noexcept;

        //! @brief 今の速度で 1 フレーム動かし、根の位置・速度・接地を書く
        //! @details 地形に当てて押し返すのは collider (Collider::Move)。持ち主が Scene に居なければ当たりを見ずに
        //! 速度ぶん進め、接地は false にし、接地の知らせは出さない
        //! @param[in,out] collider 同じ Actor の動く体の当たり。形と JoltCharacter の持ち主
        //! @param[in] dt 1 フレームの秒数
        //! @param[in] maxStepHeight 走ったまま登れる段の高さ (m)。Collider::Move へそのまま渡す
        void Move(NS::Obj::Collider& collider, float dt, float maxStepHeight) noexcept;

        //! 接地の通知の受け口。購読は後から足せる
        [[nodiscard]] BodyEvents& Events() noexcept { return m_events; }

        // TypeRegistry には登録しない。欄は持たず、古い場面に残る寸法の鍵は読み飛ばす。寸法の欄は Collider が持つ
        NS_REFLECT_NONE(Body, NS::Obj::Component)

    private:
        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false; // 直前の Move より前の接地
        BodyEvents m_events;
    };
} // namespace NS::Obj
