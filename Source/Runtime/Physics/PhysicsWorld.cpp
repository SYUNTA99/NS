#include "Runtime/Physics/PhysicsWorld.h"

#include "Runtime/Physics/SweptAABB.h"
#include "Runtime/Physics/SweptCapsule.h"

#include <algorithm>
#include <cmath>

namespace NS::Physics
{
    using NS::Core::OBB;
    using NS::Core::Sphere;

    namespace
    {
        // 平均ブロック寸法相当。 broadphase の候補数を抑えつつセル数を増やしすぎない値
        constexpr float k_GridCellSize = 2.0f;

        // capsule が motion だけ動く間に占有する swept AABB。 grid 候補絞り込みの query box に使う
        // axis=Y の縦 capsule 前提で XZ は radius、 Y は radius + halfHeight 膨張させる
        [[nodiscard]] NS::Core::AABB CapsuleSweptAABB(const Capsule& cap, const NS::Core::Vector3& motion) noexcept
        {
            const float rx = cap.radius;
            const float ry = cap.radius + cap.halfHeight;
            const float rz = cap.radius;
            const NS::Core::Vector3 a = cap.center;
            const NS::Core::Vector3 b = cap.center + motion;
            const float minX = std::min(a.x, b.x) - rx;
            const float maxX = std::max(a.x, b.x) + rx;
            const float minY = std::min(a.y, b.y) - ry;
            const float maxY = std::max(a.y, b.y) + ry;
            const float minZ = std::min(a.z, b.z) - rz;
            const float maxZ = std::max(a.z, b.z) + rz;
            NS::Core::AABB q;
            q.Center = NS::Core::Vector3{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
            q.Extents = NS::Core::Vector3{(maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f};
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
        // 空になった AABB で grid を作り直して古いセルを残さない
        m_grid.Build(m_aabbs, k_GridCellSize);
    }

    void PhysicsWorld::ReserveAabbs(std::size_t count)
    {
        m_aabbs.reserve(count);
    }

    void PhysicsWorld::AddAABB(const NS::Core::AABB& box)
    {
        m_aabbs.push_back(box);
    }

    void PhysicsWorld::AddTriangle(const Triangle& triangle)
    {
        m_triangles.push_back(triangle);
    }

    void PhysicsWorld::AddOBB(const OBB& obb)
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
        m_grid.Build(m_aabbs, k_GridCellSize);
    }

    SweepHit PhysicsWorld::SweepCapsule(const Capsule& cap, const NS::Core::Vector3& motion) const noexcept
    {
        float earliestToi = 1.0f;
        NS::Core::Vector3 hitNormal{0.0f, 0.0f, 0.0f};
        bool anyHit = false;

        const auto considerAabb = [&](const NS::Core::AABB& box) {
            float toi = 1.0f;
            NS::Core::Vector3 n{};
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
            const NS::Core::AABB queryBox = CapsuleSweptAABB(cap, motion);
            m_grid.Query(queryBox, m_candidates);
            for (const std::uint32_t index : m_candidates)
                considerAabb(m_aabbs[index]);
        }
        else
        {
            for (const NS::Core::AABB& box : m_aabbs)
                considerAabb(box);
        }

        for (const Triangle& tri : m_triangles)
        {
            float toi = 1.0f;
            NS::Core::Vector3 n{};
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
            NS::Core::Vector3 n{};
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
            NS::Core::Vector3 n{};
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
            NS::Core::Vector3 n{};
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

    bool PhysicsWorld::ProbeGround(const NS::Core::Vector3& bottomCenter, float reach) const noexcept
    {
        const NS::Core::Ray ray(bottomCenter, NS::Core::Vector3{0.0f, -1.0f, 0.0f});
        for (const NS::Core::AABB& box : m_aabbs)
        {
            float dist = 0.0f;
            if (ray.Intersects(box, dist) && dist <= reach)
                return true;
        }

        // OBB は ray を local 軸へ移し、 原点中心の local AABB へ ray test する
        for (const OBB& obb : m_obbs)
        {
            const NS::Core::Vector3 d = bottomCenter - obb.center;
            const NS::Core::Vector3 localOrigin{d.Dot(obb.axisX), d.Dot(obb.axisY), d.Dot(obb.axisZ)};
            const NS::Core::Vector3 down{0.0f, -1.0f, 0.0f};
            const NS::Core::Vector3 localDir{down.Dot(obb.axisX), down.Dot(obb.axisY), down.Dot(obb.axisZ)};
            const NS::Core::Ray localRay(localOrigin, localDir);
            const NS::Core::AABB localBox(NS::Core::Vector3{0.0f, 0.0f, 0.0f},
                                          NS::Core::Vector3{obb.halfExtentX, obb.halfExtentY, obb.halfExtentZ});
            float dist = 0.0f;
            if (localRay.Intersects(localBox, dist) && dist <= reach)
                return true;
        }
        return false;
    }

    NS::Core::Vector3 PhysicsWorld::ComputePushOut(const Capsule& cap) const noexcept
    {
        // 1 個から出た先で隣の箱と重なることがあるので反復する。4 回は substep の再掃引と同じ回数
        constexpr int k_MaxPasses = 4;
        // 接面ちょうどだと次の掃引が toi 0 で当たり直すため、わずかに離す
        constexpr float k_Separation = 1e-3f;

        NS::Core::Vector3 total{0.0f, 0.0f, 0.0f};
        Capsule moved = cap;

        // TODO: AABB channel だけ見ている。回転した箱と坂に埋まった時はまだ出られない
        for (int pass = 0; pass < k_MaxPasses; ++pass)
        {
            bool pushed = false;

            const auto considerAabb = [&](const NS::Core::AABB& box) {
                const float minX = box.Center.x - box.Extents.x;
                const float maxX = box.Center.x + box.Extents.x;
                const float minY = box.Center.y - box.Extents.y;
                const float maxY = box.Center.y + box.Extents.y;
                const float minZ = box.Center.z - box.Extents.z;
                const float maxZ = box.Center.z + box.Extents.z;

                const float cx = moved.center.x;
                const float cz = moved.center.z;
                const float segTop = moved.center.y + moved.halfHeight;
                const float segBottom = moved.center.y - moved.halfHeight;

                // IntersectsCapsuleAABB と同じギャップの形。押す向きが要るので符号を残す
                float dx = 0.0f;
                if (cx < minX)
                    dx = cx - minX;
                else if (cx > maxX)
                    dx = cx - maxX;
                float dz = 0.0f;
                if (cz < minZ)
                    dz = cz - minZ;
                else if (cz > maxZ)
                    dz = cz - maxZ;
                float dy = 0.0f;
                if (segBottom > maxY)
                    dy = segBottom - maxY;
                else if (segTop < minY)
                    dy = segTop - minY;

                const float r = moved.radius;
                const float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq >= r * r)
                    return;

                NS::Core::Vector3 push{0.0f, 0.0f, 0.0f};
                if (distSq > NS::Core::k_Epsilon * NS::Core::k_Epsilon)
                {
                    const float dist = std::sqrt(distSq);
                    const float amount = r - dist + k_Separation;
                    push = NS::Core::Vector3{dx / dist * amount, dy / dist * amount, dz / dist * amount};
                }
                else
                {
                    // 軸線分が箱の中へ入るとギャップが全て 0 になり向きが決まらない。抜けの最短の軸へ出す
                    const float exitPosX = (maxX + r) - cx;
                    const float exitNegX = cx - (minX - r);
                    const float exitPosZ = (maxZ + r) - cz;
                    const float exitNegZ = cz - (minZ - r);
                    const float exitPosY = (maxY + r) - segBottom;
                    const float exitNegY = segTop - (minY - r);

                    float best = exitPosX;
                    push = NS::Core::Vector3{exitPosX + k_Separation, 0.0f, 0.0f};
                    if (exitNegX < best)
                    {
                        best = exitNegX;
                        push = NS::Core::Vector3{-(exitNegX + k_Separation), 0.0f, 0.0f};
                    }
                    if (exitPosZ < best)
                    {
                        best = exitPosZ;
                        push = NS::Core::Vector3{0.0f, 0.0f, exitPosZ + k_Separation};
                    }
                    if (exitNegZ < best)
                    {
                        best = exitNegZ;
                        push = NS::Core::Vector3{0.0f, 0.0f, -(exitNegZ + k_Separation)};
                    }
                    if (exitPosY < best)
                    {
                        best = exitPosY;
                        push = NS::Core::Vector3{0.0f, exitPosY + k_Separation, 0.0f};
                    }
                    if (exitNegY < best)
                    {
                        best = exitNegY;
                        push = NS::Core::Vector3{0.0f, -(exitNegY + k_Separation), 0.0f};
                    }
                }

                moved.center += push;
                total += push;
                pushed = true;
            };

            if (!m_grid.IsEmpty())
            {
                const NS::Core::AABB queryBox = CapsuleSweptAABB(moved, NS::Core::Vector3{0.0f, 0.0f, 0.0f});
                m_grid.Query(queryBox, m_candidates);
                for (const std::uint32_t index : m_candidates)
                    considerAabb(m_aabbs[index]);
            }
            else
            {
                for (const NS::Core::AABB& box : m_aabbs)
                    considerAabb(box);
            }

            if (!pushed)
                break;
        }
        return total;
    }

    bool PhysicsWorld::IsEmpty() const noexcept
    {
        return m_aabbs.empty() && m_triangles.empty() && m_obbs.empty() && m_spheres.empty() && m_capsules.empty();
    }
} // namespace NS::Physics
