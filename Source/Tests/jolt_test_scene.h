#pragma once

#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <Runtime/Physics/PhysicsScene.h>

namespace NsTest
{
    [[nodiscard]] inline NS::Core::OBB ToOBB(const NS::Core::AABB& box) noexcept
    {
        NS::Core::OBB out;
        out.center = NS::Core::Vector3{box.Center.x, box.Center.y, box.Center.z};
        out.halfExtentX = box.Extents.x;
        out.halfExtentY = box.Extents.y;
        out.halfExtentZ = box.Extents.z;
        return out;
    }

    inline void AddBox(NS::Physics::PhysicsScene& physics, const NS::Core::AABB& box)
    {
        physics.AddBox(ToOBB(box), NS::Physics::ObjectLayers::Terrain);
    }
} // namespace NsTest
