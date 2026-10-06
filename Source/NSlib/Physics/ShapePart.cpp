#include "NSlib/Physics/ShapePart.h"

#include "NSlib/Physics/detail/JoltConversion.h"
#include "NSlib/Physics/detail/JoltRuntime.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

namespace NS::Phys
{
    ShapePart MakeBoxPart(const NS::OBB& box)
    {
        // collision は PhysicsScene より先に形を作ることがある。形の確保は allocator の登録が済んでいないと落ちる
        detail::InitJoltRuntime();
        const JPH::BoxShapeSettings settings{JPH::Vec3{box.halfExtentX, box.halfExtentY, box.halfExtentZ}};

        const JPH::Mat44 axes{JPH::Vec4{ToJolt(box.axisX), 0.0f},
                              JPH::Vec4{ToJolt(box.axisY), 0.0f},
                              JPH::Vec4{ToJolt(box.axisZ), 0.0f},
                              JPH::Vec4{0.0f, 0.0f, 0.0f, 1.0f}};

        return ShapePart{ShapeOrNull(settings.Create()), box.center, FromJolt(axes.GetQuaternion())};
    }

    ShapePart MakeSpherePart(const NS::Sphere& sphere)
    {
        detail::InitJoltRuntime();
        const JPH::SphereShapeSettings settings{sphere.radius};
        return ShapePart{ShapeOrNull(settings.Create()), sphere.center, NS::Quaternion::Identity};
    }

    ShapePart MakeCapsulePart(const Capsule& capsule)
    {
        detail::InitJoltRuntime();
        const JPH::CapsuleShapeSettings settings{capsule.halfHeight, capsule.radius};
        // JPH::CapsuleShape は Y 軸に沿った形なので、Y から axis へ回す
        const JPH::Quat rotation =
            JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), ToJolt(capsule.axis).NormalizedOr(JPH::Vec3::sAxisY()));
        return ShapePart{ShapeOrNull(settings.Create()), capsule.center, FromJolt(rotation)};
    }
} // namespace NS::Phys
