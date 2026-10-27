#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"

namespace NS
{
    //! 描画と独立した箱の衝突形状。WorldOBB は回転と非一様スケールを保持する
    class BoxCollision : public Collision
    {
    public:
        BoxCollision() noexcept = default;
        //! 負の成分は 0 にクランプする
        explicit BoxCollision(const Vector3& halfExtents) noexcept;

        //! 負の成分は 0 にクランプする
        void SetHalfExtents(const Vector3& halfExtents) noexcept;
        //! owner のスケールを掛ける前の各軸の半長 (m)
        [[nodiscard]] Vector3 HalfExtents() const noexcept;

        void SetCenterOffset(const Vector3& offset) noexcept;
        [[nodiscard]] Vector3 CenterOffset() const noexcept;

        void SetLocalRotation(const Quaternion& rotation) noexcept;
        [[nodiscard]] Quaternion LocalRotation() const noexcept;

        //! 局所回転をオイラー角 (度) で読み書きする
        void SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept;
        [[nodiscard]] Vector3 RotationEulerDegrees() const noexcept;

        //! world に局所オフセットと局所回転を重ねた世界空間の軸並行 AABB
        [[nodiscard]] AABB WorldAABB(const Matrix& world) const noexcept;

        //! world に局所オフセットと局所回転を重ねた世界空間の OBB (回転・非一様スケールを保持)
        [[nodiscard]] OBB WorldOBB(const Matrix& world) const noexcept;

        NS_REFLECT_BEGIN(BoxCollision, Collision)
        NS_ACCESSOR(Vector3, "半径", HalfExtents(), SetHalfExtents)
        NS_ACCESSOR(Vector3, "中心オフセット", CenterOffset(), SetCenterOffset)
        NS_ACCESSOR(Vector3, "回転 (度)", RotationEulerDegrees(), SetRotationEulerDegrees)
        NS_REFLECT_END()

    private:
        [[nodiscard]] JPH::BodyID SyncBody(Phys::PhysicsScene& physics,
                                           JPH::BodyID current,
                                           const Matrix& world) override;

        Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};          // ローカル空間 (m)
        Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};         // ローカル空間 (m)
        Quaternion m_localRotation = Quaternion::Identity; // ローカル空間
    };
} // namespace NS
