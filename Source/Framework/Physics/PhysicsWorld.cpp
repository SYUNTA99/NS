#include "Framework/Physics/PhysicsWorld.h"

namespace NS::Physics
{
    namespace
    {
        // 平均ブロック寸法相当。 broadphase の候補数を抑えつつセル数を増やしすぎない値
        constexpr float kGridCellSize = 2.0f;
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

    bool PhysicsWorld::IsEmpty() const noexcept
    {
        return m_aabbs.empty() && m_triangles.empty() && m_obbs.empty() && m_spheres.empty() && m_capsules.empty();
    }
} // namespace NS::Physics
