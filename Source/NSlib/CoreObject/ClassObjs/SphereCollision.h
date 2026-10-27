#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"

namespace NS
{
    /// 描画と独立した球の衝突形状。非一様スケールでも球を保つため最大軸を使う
    class SphereCollision : public Collision
    {
    public:
        /// 既定半径 0.5 で構築する
        SphereCollision() noexcept = default;
        /// 半径を指定して構築する。負は 0 にクランプ
        explicit SphereCollision(float radius) noexcept;

        /// 負は 0 にクランプ
        void SetRadius(float radius) noexcept;
        /// 半径を返す。owner の scale を掛ける前の値
        [[nodiscard]] float Radius() const noexcept;

        /// 持ち主の局所座標系での中心オフセットを設定 / 取得する
        void SetCenterOffset(const Vector3& offset) noexcept;
        [[nodiscard]] Vector3 CenterOffset() const noexcept;

        /// 持ち主の root の世界行列 world に offset を重ねた球を返す。半径は world の拡縮の最大成分で拡縮する
        [[nodiscard]] Sphere WorldSphere(const Matrix& world) const noexcept;

        /// 持ち主の root の世界行列 world を反映した世界軸並行 AABB を返す
        [[nodiscard]] AABB WorldAABB(const Matrix& world) const noexcept;

        NS_REFLECT_BEGIN(SphereCollision, Collision)
        NS_ACCESSOR(float, "半径", Radius(), SetRadius)
        NS_ACCESSOR(Vector3, "中心オフセット", CenterOffset(), SetCenterOffset)
        NS_REFLECT_END()

    private:
        // 世界座標の球を body 1 個として入れる
        [[nodiscard]] JPH::BodyID SyncBody(Phys::PhysicsScene& physics,
                                           JPH::BodyID current,
                                           const Matrix& world) override;

        float m_radius = 0.5f;                        // 球の半径 (owner scale 前)
        Vector3 m_centerOffset{0.0f, 0.0f, 0.0f}; // 持ち主の局所座標系での中心オフセット
    };
} // namespace NS
