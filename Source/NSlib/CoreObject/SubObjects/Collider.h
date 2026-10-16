#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IUse/IUseCollision.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/Physics/Capsule.h"
#include "NSlib/Physics/JoltCharacter.h"

#include <memory>

namespace NS
{
    namespace Phys
    {
        class PhysicsScene;
    }
} // namespace NS

namespace NS
{
    //! @brief Collider::Move が返す 1 フレームの移動の結果
    struct ColliderMove
    {
        Vector3 position{0.0f, 0.0f, 0.0f}; //!< 地形に当てて押し返した後の根の位置
        Vector3 velocity{0.0f, 0.0f, 0.0f}; //!< 接触面へ射影した後の速度
        bool grounded = false;                  //!< 移動の後に足場に立っているか
        bool inWorld = false;                   //!< 地形を見たか。持ち主が Scene に居なければ偽
    };

    //! @brief カプセルを地形に当てて移動する部品。位置・速度への反映は Body が行う
    //! @details 自分への衝突を避けるため、静的な PhysicsScene の body には登録しない
    class Collider : public SubObject, public IUseCollision
    {
    public:
        Collider() noexcept = default;

        //! 当たりのカプセルの半径 (m)。欄「半径」の値で、球にしている間も変わらない
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_radius; }
        //! @brief 当たりのカプセルの半径を置く
        //! @details 0 以下と有限でない値は捨てて元の値を残す
        //! @param[in] radius 半径 (m)
        void SetCapsuleRadius(float radius) noexcept;
        //! 今の当たりの円柱の半分の高さ。球にしていなければ StandingHalfHeight と同じ。
        //! SetSphereShape で球にしている間は 0
        [[nodiscard]] float CapsuleHalfHeight() const noexcept;
        //! @brief 移動判定と同じカプセルを返す。中心は根、軸は +Y
        //! @param[in] rootPosition 根の位置 (ワールド)
        [[nodiscard]] Phys::Capsule CapsuleAt(const Vector3& rootPosition) const noexcept;
        //! @brief 現在の根の位置に置いたカプセル。持ち主がなければ原点を使う
        [[nodiscard]] Phys::Capsule WorldCapsule() const noexcept;
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
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

        //! 位置 from から地形に当てた移動結果を返す。個体の姿勢と速度は変更しない
        //! velocity は m/s、dt は秒、maxStepHeight は登れる段差 m
        //! シーンなしでは速度分だけ進み、grounded と inWorld は false
        [[nodiscard]] ColliderMove Move(const Vector3& from,
                                        const Vector3& velocity,
                                        float dt,
                                        float maxStepHeight) noexcept;

        // TypeRegistry には登録しない。欄の表示名は保存の鍵
        NS_REFLECT_BEGIN(Collider, SubObject)
        NS_REFLECT_ACCESSOR(float, "半径", CapsuleRadius(), SetCapsuleRadius)
        NS_REFLECT_ACCESSOR(float, "半分の高さ", StandingHalfHeight(), SetStandingHalfHeight)
        NS_REFLECT_END()

    private:
        float m_radius = 0.4f;             // 当たりのカプセルの半径 (m)
        float m_standingHalfHeight = 0.5f; // 立ち姿の円柱の半分の高さ (m)。半球を除く
        bool m_sphereShape = false;        // 当たりを球にしているか。書くのは SetSphereShape だけ
        std::unique_ptr<Phys::JoltCharacter> m_character;
    };
} // namespace NS
