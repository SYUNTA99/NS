#pragma once

/// @file PhysicsWorld.h
/// @brief NS::Physics::PhysicsWorld — 静的衝突プリミティブ 5 channel + broadphase grid を所有し
///        capsule sweep / ground probe を提供する pure physics の衝突 world
///
/// 依存: Math (AABB / Vector3), Capsule, Sphere, SweptOBB (OBB), SweptTriangle (Triangle), CollisionGrid
/// pole / hazard 等 Scene gameplay は層が違うため含めない (NS::Physics は NS::Scene に依存しない)
/// collision 再構築時に Clear -> Add* -> BuildBroadphase で満たし、 SweepCapsule / ProbeGround で問い合わせる

#include "Framework/Math/Math.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Physics/CollisionGrid.h"
#include "Framework/Physics/Sphere.h"
#include "Framework/Physics/SweptOBB.h"
#include "Framework/Physics/SweptTriangle.h"

#include <cstddef>
#include <vector>

namespace NS::Physics
{
    /// capsule sweep の最初の接触結果。 hit が false の時 toi は 1.0、 normal は zero
    struct SweepHit
    {
        float toi = 1.0f;
        NS::Math::Vector3 normal{0.0f, 0.0f, 0.0f};
        bool hit = false;
    };

    /// 静的衝突プリミティブ 5 channel (AABB / Triangle / OBB / Sphere / Capsule) と AABB 専用 broadphase
    /// grid を所有する。 pole / hazard は gameplay 判定のため含めない
    class PhysicsWorld
    {
    public:
        // build (collision 再構築時に 1 度満たす)

        /// 全 channel と grid を空にする
        void Clear() noexcept;

        /// AABB channel の領域を予約する (grid 配置物数が既知の時の任意最適化)
        void ReserveAabbs(std::size_t count);

        /// 軸並行 box (grid solid) を AABB channel へ追加する
        void AddAabb(const NS::Math::AABB& box);

        /// slope の世界三角形を Triangle channel へ追加する
        void AddTriangle(const Triangle& triangle);

        /// 自由配置物 (回転 / scale 込み) を OBB channel へ追加する
        void AddObb(const OBB& obb);

        /// 球 collider を Sphere channel へ追加する
        void AddSphere(const Sphere& sphere);

        /// capsule collider を Capsule channel へ追加する
        void AddCapsule(const Capsule& capsule);

        /// AABB channel から broadphase grid を構築する。 Add 完了後に 1 度呼ぶ
        void BuildBroadphase() noexcept;

        // accessors

        /// AABB channel の参照。 blob shadow の receiver 構築など読み取り専用用途に使う
        [[nodiscard]] const std::vector<NS::Math::AABB>& Aabbs() const noexcept { return m_aabbs; }

        /// 全 channel が空か (未 build / プリミティブ無し)
        [[nodiscard]] bool IsEmpty() const noexcept;

    private:
        std::vector<NS::Math::AABB> m_aabbs;
        std::vector<Triangle> m_triangles;
        std::vector<OBB> m_obbs;
        std::vector<Sphere> m_spheres;
        std::vector<Capsule> m_capsules;
        CollisionGrid m_grid;
    };
} // namespace NS::Physics
