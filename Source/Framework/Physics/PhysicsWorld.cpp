#include "Framework/Physics/PhysicsWorld.h"

#include "Framework/Physics/SweptAABB.h"
#include "Framework/Physics/SweptCapsule.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace NS::Physics
{
    namespace
    {
        // 平均ブロック寸法相当。 broadphase の候補数を抑えつつセル数を増やしすぎない値
        constexpr float kGridCellSize = 2.0f;

        // capsule が motion だけ動く間に占有する swept AABB。 grid 候補絞り込みの query box に使う
        // 縦 capsule (axis=Y) 前提で XZ は radius、 Y は radius + halfHeight 膨張させる
        [[nodiscard]] NS::Math::AABB CapsuleSweptAabb(const Capsule& cap, const NS::Math::Vector3& motion) noexcept
        {
            const float rx = cap.radius;
            const float ry = cap.radius + cap.halfHeight;
            const float rz = cap.radius;
            const NS::Math::Vector3 a = cap.center;
            const NS::Math::Vector3 b = cap.center + motion;
            const float minX = std::min(a.x, b.x) - rx;
            const float maxX = std::max(a.x, b.x) + rx;
            const float minY = std::min(a.y, b.y) - ry;
            const float maxY = std::max(a.y, b.y) + ry;
            const float minZ = std::min(a.z, b.z) - rz;
            const float maxZ = std::max(a.z, b.z) + rz;
            NS::Math::AABB q;
            q.Center = NS::Math::Vector3{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
            q.Extents = NS::Math::Vector3{(maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f};
            return q;
        }
    } // namespace

    void PhysicsWorld::Clear() noexcept
    {
        m_aabbs.clear();
        m_triangles.clear();
        m_obbs.clear();
        m_spheres.clear();
        m_capsules.clear();
        // 空になった AABB で grid を作り直して stale セルを残さない
        m_grid.Build(m_aabbs, kGridCellSize);
    }

    void PhysicsWorld::ReserveAabbs(std::size_t count)
    {
        m_aabbs.reserve(count);
    }

    void PhysicsWorld::AddAabb(const NS::Math::AABB& box)
    {
        m_aabbs.push_back(box);
    }

    void PhysicsWorld::AddTriangle(const Triangle& triangle)
    {
        m_triangles.push_back(triangle);
    }

    void PhysicsWorld::AddObb(const OBB& obb)
    {
        m_obbs.push_back(obb);
    }

    void PhysicsWorld::AddSphere(const Sphere& sphere)
    {
        m_spheres.push_back(sphere);
    }

    void PhysicsWorld::AddCapsule(const Capsule& capsule)
    {
        m_capsules.push_back(capsule);
    }

    void PhysicsWorld::BuildBroadphase() noexcept
    {
        m_grid.Build(m_aabbs, kGridCellSize);
    }

    SweepHit PhysicsWorld::SweepCapsule(const Capsule& cap, const NS::Math::Vector3& motion) const noexcept
    {
        float earliestToi = 1.0f;
        NS::Math::Vector3 hitNormal{0.0f, 0.0f, 0.0f};
        bool anyHit = false;

        const auto considerAabb = [&](const NS::Math::AABB& box) {
            float toi = 1.0f;
            NS::Math::Vector3 n{};
            if (SweptCapsuleVsAABB(cap, motion, box, toi, n) && toi < earliestToi)
            {
                earliestToi = toi;
                hitNormal = n;
                anyHit = true;
            }
        };

        // AABB channel: grid があれば swept AABB 近傍の候補だけ、 無ければ総当たり
        if (!m_grid.IsEmpty())
        {
            const NS::Math::AABB queryBox = CapsuleSweptAabb(cap, motion);
            std::vector<std::uint32_t> candidates;
            m_grid.Query(queryBox, candidates);
            for (const std::uint32_t idx : candidates)
                considerAabb(m_aabbs[idx]);
        }
        else
        {
            for (const NS::Math::AABB& box : m_aabbs)
                considerAabb(box);
        }

        for (const Triangle& tri : m_triangles)
        {
            float toi = 1.0f;
            NS::Math::Vector3 n{};
            if (SweptCapsuleVsTriangle(cap, motion, tri, toi, n) && toi < earliestToi)
            {
                earliestToi = toi;
                hitNormal = n;
                anyHit = true;
            }
        }

        for (const OBB& obb : m_obbs)
        {
            float toi = 1.0f;
            NS::Math::Vector3 n{};
            if (SweptCapsuleVsOBB(cap, motion, obb, toi, n) && toi < earliestToi)
            {
                earliestToi = toi;
                hitNormal = n;
                anyHit = true;
            }
        }

        for (const Sphere& sphere : m_spheres)
        {
            float toi = 1.0f;
            NS::Math::Vector3 n{};
            if (SweptCapsuleVsSphere(cap, motion, sphere, toi, n) && toi < earliestToi)
            {
                earliestToi = toi;
                hitNormal = n;
                anyHit = true;
            }
        }

        for (const Capsule& other : m_capsules)
        {
            float toi = 1.0f;
            NS::Math::Vector3 n{};
            if (SweptCapsuleVsCapsule(cap, motion, other, toi, n) && toi < earliestToi)
            {
                earliestToi = toi;
                hitNormal = n;
                anyHit = true;
            }
        }

        SweepHit result;
        result.toi = earliestToi;
        result.normal = hitNormal;
        result.hit = anyHit;
        return result;
    }

    bool PhysicsWorld::ProbeGround(const NS::Math::Vector3& bottomCenter, float reach) const noexcept
    {
        const NS::Math::Ray ray(bottomCenter, NS::Math::Vector3{0.0f, -1.0f, 0.0f});
        for (const NS::Math::AABB& box : m_aabbs)
        {
            float dist = 0.0f;
            if (ray.Intersects(box, dist) && dist <= reach)
                return true;
        }

        // OBB は ray を local 軸へ移し、 原点中心の local AABB へ ray test する
        for (const OBB& obb : m_obbs)
        {
            const NS::Math::Vector3 d = bottomCenter - obb.center;
            const NS::Math::Vector3 localOrigin{d.Dot(obb.axisX), d.Dot(obb.axisY), d.Dot(obb.axisZ)};
            const NS::Math::Vector3 down{0.0f, -1.0f, 0.0f};
            const NS::Math::Vector3 localDir{down.Dot(obb.axisX), down.Dot(obb.axisY), down.Dot(obb.axisZ)};
            const NS::Math::Ray localRay(localOrigin, localDir);
            const NS::Math::AABB localBox(NS::Math::Vector3{0.0f, 0.0f, 0.0f}, obb.halfExtents);
            float dist = 0.0f;
            if (localRay.Intersects(localBox, dist) && dist <= reach)
                return true;
        }
        return false;
    }

    bool PhysicsWorld::IsEmpty() const noexcept
    {
        return m_aabbs.empty() && m_triangles.empty() && m_obbs.empty() && m_spheres.empty() && m_capsules.empty();
    }
} // namespace NS::Physics
