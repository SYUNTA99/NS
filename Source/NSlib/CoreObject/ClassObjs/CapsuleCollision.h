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
        //! 既定 半径 0.4 / 半高 0.5 の縦 capsule で構築する
        CapsuleCollision() noexcept = default;
        //! 半径 / 半高を指定して構築する。負は 0 にクランプ
        CapsuleCollision(float radius, float halfHeight) noexcept;

        //! 負は 0 にクランプ
        void SetRadius(float radius) noexcept;
        //! 半径を返す。owner の scale を掛ける前の値
        [[nodiscard]] float Radius() const noexcept;

        //! 軸線分の半分の長さ、半球を除く円柱部の半長。負は 0 にクランプ
        void SetHalfHeight(float halfHeight) noexcept;
        //! 円柱部の半長を返す。owner の scale を掛ける前の値
        [[nodiscard]] float HalfHeight() const noexcept;

        //! 持ち主の局所座標系での中心オフセット
        void SetCenterOffset(const Vector3& offset) noexcept;
        [[nodiscard]] Vector3 CenterOffset() const noexcept;

        //! 局所回転をクォータニオンで直接設定 / 取得する
        void SetLocalRotation(const Quaternion& rotation) noexcept;
        [[nodiscard]] Quaternion LocalRotation() const noexcept;

        //! 局所回転を仰角・方位角・傾きのオイラー角 (度) で読み書きする。寝かせ用に内部はクォータニオン保持
        void SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept;
        [[nodiscard]] Vector3 RotationEulerDegrees() const noexcept;

        //! 持ち主の root の世界行列 world と局所変換を重ねる
        [[nodiscard]] Phys::Capsule WorldCapsule(const Matrix& world) const noexcept;

        //! 持ち主の root の世界行列 world を反映した世界軸並行 AABB を返す
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

        float m_radius = 0.4f;                                     // capsule 半径
        float m_halfHeight = 0.5f;                                 // 円柱部の半長、半球を除く
        Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};              // 持ち主の局所座標系での中心オフセット
        Quaternion m_localRotation = Quaternion::Identity; // owner 回転に重ねる局所回転
    };
} // namespace NS
