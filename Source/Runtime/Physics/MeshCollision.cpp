#include "Runtime/Physics/MeshCollision.h"

#include "Runtime/Physics/detail/JoltConversion.h"
#include "Runtime/Physics/detail/JoltRuntime.h"

#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <utility>

namespace NS::Physics
{
    JPH::ShapeRefC CreateMeshShape(std::span<const Triangle> triangles)
    {
        if (triangles.empty())
            return nullptr;

        // 資産の読込は PhysicsScene より先に来ることがある。 形の確保は allocator の登録が済んでいないと落ちる
        detail::InitializeJoltRuntime();
        JPH::TriangleList list;
        list.reserve(triangles.size());
        for (const Triangle& triangle : triangles)
            list.emplace_back(ToJolt(triangle.v0), ToJolt(triangle.v1), ToJolt(triangle.v2));

        const JPH::MeshShapeSettings settings{std::move(list)};
        const JPH::ShapeSettings::ShapeResult shape = settings.Create();
        if (shape.HasError())
            return nullptr;
        return shape.Get();
    }
} // namespace NS::Physics
