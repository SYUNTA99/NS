#include "Runtime/Physics/ShapePart.h"

#include "Runtime/Physics/detail/JoltConversion.h"
#include "Runtime/Physics/detail/JoltRuntime.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

namespace NS::Phys
{
    namespace
    {
        // 失敗した形は null で返す。呼出側は形の null だけを見れば、作れたかが分かる
        [[nodiscard]] JPH::ShapeRefC ShapeOrNull(const JPH::ShapeSettings::ShapeResult& result)
        {
            if (result.HasError())
            {
                return nullptr;
            }
            return result.Get();
        }
    } // namespace

    ShapePart MakeBoxPart(const NS::Core::OBB& box)
    {
        // collider は PhysicsScene より先に形を作ることがある。形の確保は allocator の登録が済んでいないと落ちる
        detail::InitJoltRuntime();
        const JPH::BoxShapeSettings settings{JPH::Vec3{box.halfExtentX, box.halfExtentY, box.halfExtentZ}};

        const JPH::Mat44 axes{JPH::Vec4{ToJolt(box.axisX), 0.0f},
                              JPH::Vec4{ToJolt(box.axisY), 0.0f},
                              JPH::Vec4{ToJolt(box.axisZ), 0.0f},
                              JPH::Vec4{0.0f, 0.0f, 0.0f, 1.0f}};

        return ShapePart{ShapeOrNull(settings.Create()), box.center, FromJolt(axes.GetQuaternion())};
    }

    ShapePart MakeSpherePart(const NS::Core::Sphere& sphere)
    {
        detail::InitJoltRuntime();
        const JPH::SphereShapeSettings settings{sphere.radius};
        return ShapePart{ShapeOrNull(settings.Create()), sphere.center, NS::Core::Quaternion::Identity};
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
