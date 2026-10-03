#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/BodyEvents.h"
#include "Runtime/Object/IUse/IUseCollision.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/JoltCharacter.h"

#include <memory>

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Obj
{
    //! @brief 登場人物の身体。移動と接地の部品
    //! @details 自機も敵も同じ身体を固定の部品として持ち、自分の状態から呼ぶ。派生させない。
    //! 速度の横縦分解・接地・自分の当たりのカプセルの寸法・1 フレームの移動だけを持ち、
    //! 能力も調整値も持たない。いつ何を呼ぶかは持ち主の Actor が決める。更新の入口 (OnUpdate) は持たない。
    //! カプセルは根を中心にした縦向きで、寸法 (半径・半分の高さ) はリフレクションの欄として自分が持つ。
    //! 同じ Actor の他の部品から借りないので、持ち主が無くても OnStart の前でも欄の値を返す。
    //! TypeRegistry には登録しない。部品名は持ち主が ForEachPart で付ける。
    //! IUseCollision を継ぎ、PhysicsScene は使う時に持ち主の Scene から引く。
    //! 体の周りの地形は、この部品を渡して RaycastCollision・OverlapBoxCollision で問う。
    //! JoltCharacter だけは作った時の PhysicsScene を持ち続ける。
    //! dt は呼び手が引数で渡す。呼び手は固定ステップの秒を渡し、描画フレームの秒は渡さない
    //! 依存: NS::Core, NS::Phys::JoltCharacter / PhysicsScene, NS::Obj::Actor / IUseCollision
    class Body : public NS::Obj::Component, public NS::Obj::IUseCollision
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

        //! 当たりのカプセルの半径 (m)。欄「半径」の値で、球にしている間も変わらない
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_radius; }
        //! @brief 当たりのカプセルの半径を置く
        //! @details 負は 0 にし、有限でない値は捨てて元の値を残す
        //! @param[in] radius 半径 (m)
        void SetCapsuleRadius(float radius) noexcept;
        //! 今の当たりの円柱の半分の高さ。球にしていなければ StandingHalfHeight と同じ。
        //! SetSphereShape で球にしている間は 0
        [[nodiscard]] float CapsuleHalfHeight() const noexcept;
        //! @brief 根を rootPosition に置いた時の、今の当たりの形を返す
        //! @details 中心は根、軸は +Y、半分の高さは CapsuleHalfHeight()、半径は CapsuleRadius()。
        //! JoltCharacter を作る Move と同じ形で、球にしている間は半分の高さが 0。判定・矢印・エディタの線は
        //! 当たりの形をここから引き、軸や中心の決まりを自分で組み直さない
        //! @param[in] rootPosition 根の位置 (ワールド)
        [[nodiscard]] NS::Phys::Capsule CapsuleAt(const NS::Core::Vector3& rootPosition) const noexcept;
        //! @brief 根の今の位置での当たりの形を返す。CapsuleAt(根の位置) と同じ
        //! @details 自機の体のセンサーが毎回これを読むので、寸法の欄を変えたその場で範囲の照合に効く。
        //! 持ち主が無ければ原点に置く
        [[nodiscard]] NS::Phys::Capsule WorldCapsule() const noexcept;
        //! 立ち姿の円柱の半分の高さ (m)。欄「半分の高さ」の値。
        //! 球にしている間も変わらないので、立ち姿の寸法はここから引く
        [[nodiscard]] float StandingHalfHeight() const noexcept { return m_standingHalfHeight; }
        //! @brief 立ち姿の円柱の半分の高さを置く
        //! @details 負は 0 にし、有限でない値は捨てて元の値を残す
        //! @param[in] halfHeight 半分の高さ (m)
        void SetStandingHalfHeight(float halfHeight) noexcept;

        //! 持ち主の Scene の衝突の PhysicsScene。持ち主が無いか Scene に居なければ nullptr
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

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

        //! JoltCharacter を 1 フレーム進め、位置・速度・接地を更新する
        //! 持ち主が Scene に居なければ当たりを見ずに速度ぶん進め、接地は false にする
        //! maxStepHeight は走ったまま登れる段の高さ (m) で、JoltCharacter::Step へそのまま渡す
        void Move(float dt, float maxStepHeight) noexcept;

        //! 接地の通知の受け口。購読は後から足せる
        [[nodiscard]] BodyEvents& Events() noexcept { return m_events; }

        // TypeRegistry には登録しない。欄の表示名は保存の鍵
        NS_REFLECT_BEGIN(Body, NS::Obj::Component)
        NS_REFLECT_ACCESSOR(float, "半径", CapsuleRadius(), SetCapsuleRadius)
        NS_REFLECT_ACCESSOR(float, "半分の高さ", StandingHalfHeight(), SetStandingHalfHeight)
        NS_REFLECT_END()

        //! @brief 当たりを円柱の長さ 0 のカプセル (半径が同じ球) にするかを切り替える
        //! @details 真の間は CapsuleHalfHeight が 0 を返し、移動と裁定が同じ球で当たる。半径は変えない。
        //! 根の位置は動かさないので、足元を揃えるのは呼び手の仕事
        void SetSphereShape(bool sphere) noexcept;

    private:
        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        bool m_isGrounded = false;
        bool m_wasGrounded = false;        // 直前の Move より前の接地
        float m_radius = 0.4f;             // 当たりのカプセルの半径 (m)
        float m_standingHalfHeight = 0.5f; // 立ち姿の円柱の半分の高さ (m)。半球を除く
        std::unique_ptr<NS::Phys::JoltCharacter> m_character;
        BodyEvents m_events;
        bool m_sphereShape = false; // 当たりを球にしているか。書くのは SetSphereShape だけ
    };
} // namespace NS::Obj
