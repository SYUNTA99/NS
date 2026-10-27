#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"
#include "NSlib/Phys/Capsule.h"

namespace NS
{
    //! 地形として登録する Y 軸方向のカプセル。動く身体には Collider を使う
    //! 半径は XZ の最大スケール、半長は Y で拡縮する。回転と非一様スケールの併用は近似
    class CapsuleCollision : public Collision
    {
    public:
        CapsuleCollision() noexcept = default;
        //! 負の値は 0 にクランプする
        CapsuleCollision(float radius, float halfHeight) noexcept;

        //! 負の値は 0 にクランプする
        void SetRadius(float radius) noexcept;
        //! owner のスケールを掛ける前の半径 (m)
        [[nodiscard]] float Radius() const noexcept;

        //! 半球を除く円柱部の半長を設定する。負の値は 0 にクランプする
        void SetHalfHeight(float halfHeight) noexcept;
        //! owner のスケールを掛ける前の円柱部の半長 (m)
        [[nodiscard]] float HalfHeight() const noexcept;

        void SetCenterOffset(const Vector3& offset) noexcept;
        [[nodiscard]] Vector3 CenterOffset() const noexcept;

        void SetLocalRotation(const Quaternion& rotation) noexcept;
        [[nodiscard]] Quaternion LocalRotation() const noexcept;

        //! 局所回転をオイラー角 (度) で読み書きする
        void SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept;
        [[nodiscard]] Vector3 RotationEulerDegrees() const noexcept;

        //! world に局所オフセットと局所回転を重ねた世界空間のカプセル
        [[nodiscard]] Phys::Capsule WorldCapsule(const Matrix& world) const noexcept;

        //! world を反映した世界空間の軸並行 AABB
        [[nodiscard]] AABB WorldAABB(const Matrix& world) const noexcept;

        NS_REFLECT_BEGIN(CapsuleCollision, Collision)
        NS_ACCESSOR(float, "半径", Radius(), SetRadius)
        NS_ACCESSOR(float, "半分の高さ", HalfHeight(), SetHalfHeight)
        NS_ACCESSOR(Vector3, "中心オフセット", CenterOffset(), SetCenterOffset)
        NS_ACCESSOR(Vector3, "回転 (度)", RotationEulerDegrees(), SetRotationEulerDegrees)
        NS_REFLECT_END()

    private:
        [[nodiscard]] JPH::BodyID SyncBody(Phys::PhysicsScene& physics,
                                           JPH::BodyID current,
                                           const Matrix& world) override;

        float m_radius = 0.4f;                             // ローカル空間 (m)
        float m_halfHeight = 0.5f;                         // 半球を除く円柱部の半長、ローカル空間 (m)
        Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};         // ローカル空間 (m)
        Quaternion m_localRotation = Quaternion::Identity; // ローカル空間
    };
} // namespace NS
