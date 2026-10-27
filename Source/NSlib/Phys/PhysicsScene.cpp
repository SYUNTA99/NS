#include "NSlib/Phys/PhysicsScene.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Phys/detail/JoltConversion.h"
#include "NSlib/Phys/ShapePart.h"
#include "NSlib/Phys/detail/JoltRuntime.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

namespace NS::Phys
{
        namespace
        {
            constexpr uint k_MaxBodies = 4096;
            // body はどれも動かず、body どうしは当たらないので、接触の組と拘束は作られない
            constexpr uint k_MaxBodyPairs = 0;
            constexpr uint k_MaxContactConstraints = 0;
            constexpr uint k_MaxJobs = 1024;
            constexpr std::size_t k_TempAllocatorBytes = 10 * 1024 * 1024;
        } // namespace

        PhysicsScene::RuntimeInit::RuntimeInit()
        {
            detail::InitJoltRuntime();
        }

        uint PhysicsScene::BPLayerInterface::GetNumBroadPhaseLayers() const
        {
            return BroadPhaseLayers::Count;
        }

        JPH::BroadPhaseLayer PhysicsScene::BPLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
        {
            JPH_ASSERT(layer < ObjectLayers::Count);
            return BroadPhaseLayers::FromObjectLayer(layer);
        }

        bool PhysicsScene::ObjLayerPairFilter::ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const
        {
            // body どうしの当たりは剛体を進める時だけに使われる。光線・バウンディングボックスの問い合わせと自機の掃引は、この判定を見ない
            (void)first;
            (void)second;
            return false;
        }

        bool PhysicsScene::ObjVsBPLayerFilter::ShouldCollide(JPH::ObjectLayer object,
                                                             JPH::BroadPhaseLayer broadPhase) const
        {
            return ObjLayerPairFilter{}.ShouldCollide(object, static_cast<JPH::ObjectLayer>(broadPhase.GetValue()));
        }

        PhysicsScene::PhysicsScene() : m_tempAllocator(k_TempAllocatorBytes), m_jobSystem(k_MaxJobs)
        {
            m_physicsSystem.Init(k_MaxBodies,
                                 0,
                                 k_MaxBodyPairs,
                                 k_MaxContactConstraints,
                                 m_broadPhaseLayerInterface,
                                 m_objectVsBroadPhaseLayerFilter,
                                 m_objectLayerPairFilter);
        }

        PhysicsScene::~PhysicsScene() = default;

        uint PhysicsScene::BodyCount() const noexcept { return m_physicsSystem.GetNumBodies(); }

        uint PhysicsScene::AttachedBodyCount() const
        {
            JPH::BodyIDVector ids;
            m_physicsSystem.GetBodies(ids);
            const JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
            uint count = 0;
            for (const JPH::BodyID id : ids)
            {
                if (bodies.IsAdded(id))
                {
                    ++count;
                }
            }
            return count;
        }

        JPH::BodyID PhysicsScene::SyncStatic(JPH::BodyID id,
                                             const JPH::ShapeRefC& shape,
                                             const Vector3& position,
                                             const Quaternion& rotation,
                                             JPH::ObjectLayer layer)
        {
            if (shape == nullptr)
            {
                return JPH::BodyID{};
            }

            if (id.IsInvalid())
            {
                const JPH::BodyCreationSettings settings{
                    shape, ToJolt(position), ToJolt(rotation), JPH::EMotionType::Static, layer};
                return m_physicsSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
            }

            JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
            bodies.SetShape(id, shape, false, JPH::EActivation::DontActivate);
            bodies.SetPositionAndRotationWhenChanged(
                id, ToJolt(position), ToJolt(rotation), JPH::EActivation::DontActivate);
            bodies.SetObjectLayer(id, layer);

            return id;
        }

        JPH::BodyID PhysicsScene::AddBox(const OBB& box, JPH::ObjectLayer layer)
        {
            return SyncBox(JPH::BodyID{}, box, layer);
        }

        JPH::BodyID PhysicsScene::SyncBox(JPH::BodyID id, const OBB& box, JPH::ObjectLayer layer)
        {
            const ShapePart part = MakeBoxPart(box);
            return SyncStatic(id, part.shape, part.position, part.rotation, layer);
        }

        JPH::BodyID PhysicsScene::AddSphere(const Sphere& sphere, JPH::ObjectLayer layer)
        {
            return SyncSphere(JPH::BodyID{}, sphere, layer);
        }

        JPH::BodyID PhysicsScene::SyncSphere(JPH::BodyID id, const Sphere& sphere, JPH::ObjectLayer layer)
        {
            const ShapePart part = MakeSpherePart(sphere);
            return SyncStatic(id, part.shape, part.position, part.rotation, layer);
        }

        JPH::BodyID PhysicsScene::AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer)
        {
            return SyncCapsule(JPH::BodyID{}, capsule, layer);
        }

        JPH::BodyID PhysicsScene::SyncCapsule(JPH::BodyID id, const Capsule& capsule, JPH::ObjectLayer layer)
        {
            const ShapePart part = MakeCapsulePart(capsule);
            return SyncStatic(id, part.shape, part.position, part.rotation, layer);
        }

        JPH::BodyID PhysicsScene::AddMesh(std::span<const Triangle> triangles, JPH::ObjectLayer layer)
        {
            return SyncMesh(JPH::BodyID{}, triangles, layer);
        }

        JPH::BodyID PhysicsScene::SyncMesh(JPH::BodyID id, std::span<const Triangle> triangles, JPH::ObjectLayer layer)
        {
            return SyncStatic(
                id, CreateMeshShape(triangles), Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity, layer);
        }

        JPH::BodyID PhysicsScene::SyncMeshShape(JPH::BodyID id,
                                                const MeshShape& collision,
                                                const Vector3& position,
                                                const Quaternion& rotation,
                                                const Vector3& scale,
                                                JPH::ObjectLayer layer)
        {
            if (collision.shape == nullptr)
            {
                return JPH::BodyID{};
            }

            // 拡縮が 1 なら ScaleShape は共有の形そのものを返す
            return SyncStatic(id, ShapeOrNull(collision.shape->ScaleShape(ToJolt(scale))), position, rotation, layer);
        }

        void PhysicsScene::OptimizeBroadPhase()
        {
            m_physicsSystem.OptimizeBroadPhase();
        }

        void PhysicsScene::Update(float deltaTime)
        {
            // 動いている体が無いので、Jolt はシミュレーションを飛ばして broadphase の木だけを組み直す
            m_physicsSystem.Update(deltaTime, 1, &m_tempAllocator, &m_jobSystem);
        }

        void PhysicsScene::RemoveBody(JPH::BodyID id)
        {
            if (id.IsInvalid())
            {
                return;
            }

            JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
            // 外してある body を Jolt の RemoveBody へ渡すと、入っていない物を外す事になり止まる
            if (bodies.IsAdded(id))
            {
                bodies.RemoveBody(id);
            }
            bodies.DestroyBody(id);
        }

        void PhysicsScene::DetachBody(JPH::BodyID id)
        {
            if (id.IsInvalid())
            {
                return;
            }

            JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
            if (bodies.IsAdded(id))
            {
                bodies.RemoveBody(id);
            }
        }

        void PhysicsScene::AttachBody(JPH::BodyID id)
        {
            if (id.IsInvalid())
            {
                return;
            }

            JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
            if (!bodies.IsAdded(id))
            {
                bodies.AddBody(id, JPH::EActivation::DontActivate);
            }
        }

        bool PhysicsScene::Raycast(const Vector3& origin,
                                   const Vector3& direction,
                                   float maxDistance,
                                   float& outDistance) const
        {
            const float directionLength = direction.Length();
            if (!IsPositiveFinite(maxDistance) || !IsPositiveFinite(directionLength) || !IsFinite(origin))
            {
                return false;
            }
            const JPH::RRayCast ray{ToJolt(origin), ToJolt(direction * (maxDistance / directionLength))};
            JPH::RayCastResult hit;
            if (!m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit))
            {
                return false;
            }
            outDistance = hit.mFraction * maxDistance;
            return true;
        }

        bool PhysicsScene::Raycast(const Vector3& origin,
                                   const Vector3& direction,
                                   float maxDistance,
                                   float& outDistance,
                                   Vector3& outNormal,
                                   JPH::BodyID ignoredBody) const
        {
            const float directionLength = direction.Length();
            if (!IsPositiveFinite(maxDistance) || !IsPositiveFinite(directionLength) || !IsFinite(origin))
            {
                return false;
            }

            const JPH::RRayCast ray{ToJolt(origin), ToJolt(direction * (maxDistance / directionLength))};
            JPH::RayCastResult hit;
            const JPH::IgnoreSingleBodyFilter filter{ignoredBody};
            if (!m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter))
            {
                return false;
            }

            const JPH::BodyLockRead lock{m_physicsSystem.GetBodyLockInterface(), hit.mBodyID};
            if (!lock.Succeeded())
            {
                return false;
            }

            outDistance = hit.mFraction * maxDistance;
            outNormal =
                FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
            return true;
        }

        bool PhysicsScene::OverlapsCapsule(const Capsule& capsule) const
        {
            const ShapePart part = MakeCapsulePart(capsule);
            if (part.shape == nullptr)
            {
                return false;
            }
            const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJolt(part.rotation), ToJolt(part.position));
            JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
            m_physicsSystem.GetNarrowPhaseQuery().CollideShape(
                part.shape, JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings{}, JPH::RVec3::sZero(), collector);
            return collector.HadHit();
        }
} // namespace NS::Phys
