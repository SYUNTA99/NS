#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Phys/Capsule.h"
#include "NSlib/Phys/JoltCharacter.h"

#include <memory>

namespace NS
{
    namespace Phys
    {
        class PhysicsScene;
    }
    class Transform;
} // namespace NS

namespace NS
{
    //! Collider::Move が返す 1 フレームの移動の結果
    struct ColliderMove
    {
        Vector3 position{0.0f, 0.0f, 0.0f}; // 地形に当てて押し返した後の根の位置
        Vector3 velocity{0.0f, 0.0f, 0.0f}; // 接触面へ射影した後の速度
        bool grounded = false;                  // 移動の後に足場に立っているか
        bool inWorld = false;                   // 地形を見たか。PhysicsScene を受け取っていなければ偽
        bool blocked = false;                   // 速度の向きへ進むのを地形の面が遮ったか。面に沿う動きは数えない
        Vector3 blockNormal{0.0f, 0.0f, 0.0f};  // 遮った面のうち速度に一番向かい合った面の外向きの単位法線
    };

    //! カプセルを地形に当てて移動する部品。位置・速度への反映は Body が行う
    //! 自分への衝突を避けるため、静的な PhysicsScene の body には登録しない
    class Collider : public Object, public IGetCollision
    {
    public:
        //! @param[in] physics 当たる地形の PhysicsScene。シーンへ入らないアクターは nullptr で、Move は地形を見ない
        //! @param[in] pose 根の姿勢。WorldCapsule の中心を引く。nullptr なら原点
        Collider(Phys::PhysicsScene* physics, const Transform* pose) noexcept : m_physics(physics), m_pose(pose) {}

        //! 当たりのカプセルの半径 (m)。欄「半径」の値で、球にしている間も変わらない
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_radius; }
        //! 当たりのカプセルの半径を置く
        //! 0 以下と有限でない値は捨てて元の値を残す
        //! @param[in] radius 半径 (m)
        void SetCapsuleRadius(float radius) noexcept;
        //! 今の当たりの円柱の半分の高さ。球にしていなければ StandingHalfHeight と同じ。
        //! SetSphereShape で球にしている間は 0
        [[nodiscard]] float CapsuleHalfHeight() const noexcept;
        //! 移動判定と同じカプセルを返す。中心は根、軸は +Y
        //! @param[in] rootPosition 根の位置 (ワールド)
        [[nodiscard]] Phys::Capsule CapsuleAt(const Vector3& rootPosition) const noexcept;
        //! 現在の根の位置に置いたカプセル。姿勢を受け取っていなければ原点を使う
        [[nodiscard]] Phys::Capsule WorldCapsule() const noexcept;
        //! 立ち姿の円柱の半分の高さ (m)。欄「半分の高さ」の値。
        //! 球にしている間も変わらないので、立ち姿の寸法はここから引く
        [[nodiscard]] float StandingHalfHeight() const noexcept { return m_standingHalfHeight; }
        //! 立ち姿の円柱の半分の高さを置く
        //! 負は 0 にし、有限でない値は捨てて元の値を残す
        //! @param[in] halfHeight 半分の高さ (m)
        void SetStandingHalfHeight(float halfHeight) noexcept;

        //! 当たりを円柱の長さ 0 のカプセル (半径が同じ球) にするかを切り替える
        //! 真の間は CapsuleHalfHeight が 0 を返し、移動と裁定が同じ球で当たる。半径は変えない。
        //! 根の位置は動かさないので、足元を揃えるのは呼び手の仕事
        void SetSphereShape(bool sphere) noexcept;
        //! 当たりを球にしているか。SetSphereShape で最後に置いた値
        [[nodiscard]] bool IsSphereShape() const noexcept { return m_sphereShape; }

        //! 作る時に受け取った PhysicsScene。シーンへ入らないアクターの物は nullptr
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override { return m_physics; }

        //! 位置 from から地形に当てた移動結果を返す。個体の姿勢と速度は変更しない
        //! velocity は m/s、dt は秒、maxStepHeight は登れる段差 m、maxSlopeRadians は登れる坂の上限の角度
        //! シーンなしでは速度分だけ進み、grounded と inWorld は false
        [[nodiscard]] ColliderMove Move(const Vector3& from,
                                        const Vector3& velocity,
                                        float dt,
                                        float maxStepHeight,
                                        float maxSlopeRadians) noexcept;

        // TypeRegistry には登録しない。欄の表示名は保存の鍵
        NS_REFLECT_BEGIN(Collider, Object)
        NS_ACCESSOR(float, "半径", CapsuleRadius(), SetCapsuleRadius)
        NS_ACCESSOR(float, "半分の高さ", StandingHalfHeight(), SetStandingHalfHeight)
        NS_REFLECT_END()

    private:
        float m_radius = 0.4f;             // 当たりのカプセルの半径 (m)
        float m_standingHalfHeight = 0.5f; // 立ち姿の円柱の半分の高さ (m)。半球を除く
        bool m_sphereShape = false;        // 書くのは SetSphereShape だけ
        std::unique_ptr<Phys::JoltCharacter> m_character;
        Phys::PhysicsScene* m_physics = nullptr; // 当たる地形 (非所有)。シーンが Collider より後に壊す
        const Transform* m_pose = nullptr;       // 持ち主の根 (非所有)
    };
} // namespace NS
