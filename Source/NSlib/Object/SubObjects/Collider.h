#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/IUse/IUseCollision.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Physics/Capsule.h"
#include "NSlib/Physics/JoltCharacter.h"

#include <memory>

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Obj
{
    //! @brief Collider::Move が返す 1 フレームの移動の結果
    struct ColliderMove
    {
        NS::Vector3 position{0.0f, 0.0f, 0.0f}; //!< 地形に当てて押し返した後の根の位置
        NS::Vector3 velocity{0.0f, 0.0f, 0.0f}; //!< 接触面へ射影した後の速度
        bool grounded = false;                  //!< 移動の後に足場に立っているか
        bool inWorld = false;                   //!< 地形を見たか。持ち主が Scene に居なければ偽
    };

    //! @brief 動く体の当たり。カプセルの寸法と、地形に当てて押し返す 1 フレームの移動の部品
    //! @details 世界に問う側で、自分は静的な世界 (PhysicsScene の body) に登録しない。
    //! 登録すると自分の掃引が自分に当たる。
    //! 置く当たり Collision とは別の型。部品名は持ち主が作る時に付ける
    //! カプセルは根を中心にした縦向きで、寸法 (半径・半分の高さ) はリフレクションの欄として自分が持つ。
    //! 持ち主が無くても OnStart の前でも欄の値を返す。速度と接地は持たず、Body が Move の結果を書く。
    //! IUseCollision を継ぎ、PhysicsScene は使う時に持ち主の Scene から引く。
    //! 体の周りの地形は、この部品を渡して RaycastCollision・OverlapBoxCollision で問う。
    //! JoltCharacter だけは作った時の PhysicsScene を持ち続ける。TypeRegistry には登録しない
    //! 依存: NS, NS::Phys::JoltCharacter / PhysicsScene, NS::Obj::Actor / IUseCollision
    class Collider : public NS::Obj::SubObject, public NS::Obj::IUseCollision
    {
    public:
        Collider() noexcept = default;

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
        [[nodiscard]] NS::Phys::Capsule CapsuleAt(const NS::Vector3& rootPosition) const noexcept;
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

        //! @brief 当たりを円柱の長さ 0 のカプセル (半径が同じ球) にするかを切り替える
        //! @details 真の間は CapsuleHalfHeight が 0 を返し、移動と裁定が同じ球で当たる。半径は変えない。
        //! 根の位置は動かさないので、足元を揃えるのは呼び手の仕事
        void SetSphereShape(bool sphere) noexcept;

        //! 持ち主の Scene の衝突の PhysicsScene。持ち主が無いか Scene に居なければ nullptr
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

        //! @brief 今の当たりの形を from から velocity × dt だけ動かし、地形に当てて押し返した結果を返す
        //! @details 根の位置も速度も書かない。書くのは結果を受けた Body。
        //! 持ち主が Scene に居なければ当たりを見ずに速度ぶん進め、接地は偽、inWorld も偽で返す
        //! @param[in] from 動かす前の根の位置 (ワールド)
        //! @param[in] velocity 動かす前の速度 (m/s)
        //! @param[in] dt 1 フレームの秒数。呼び手は固定ステップの秒を渡す
        //! @param[in] maxStepHeight 走ったまま登れる段の高さ (m)。JoltCharacter::Step へそのまま渡す
        //! @return 押し返した後の根の位置・速度・接地
        [[nodiscard]] ColliderMove Move(const NS::Vector3& from,
                                        const NS::Vector3& velocity,
                                        float dt,
                                        float maxStepHeight) noexcept;

        // TypeRegistry には登録しない。欄の表示名は保存の鍵
        NS_REFLECT_BEGIN(Collider, NS::Obj::SubObject)
        NS_REFLECT_ACCESSOR(float, "半径", CapsuleRadius(), SetCapsuleRadius)
        NS_REFLECT_ACCESSOR(float, "半分の高さ", StandingHalfHeight(), SetStandingHalfHeight)
        NS_REFLECT_END()

    private:
        float m_radius = 0.4f;             // 当たりのカプセルの半径 (m)
        float m_standingHalfHeight = 0.5f; // 立ち姿の円柱の半分の高さ (m)。半球を除く
        bool m_sphereShape = false;        // 当たりを球にしているか。書くのは SetSphereShape だけ
        std::unique_ptr<NS::Phys::JoltCharacter> m_character;
    };
} // namespace NS::Obj
