#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Physics/detail/JoltConversion.h"
#include "Runtime/Physics/detail/JoltRuntime.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <utility>

namespace NS::Physics
{
    namespace
    {
        constexpr JPH::uint k_MaxBodies = 4096;
        constexpr JPH::uint k_MaxBodyPairs = 16384;
        constexpr JPH::uint k_MaxContactConstraints = 4096;
        constexpr JPH::uint k_MaxJobs = 1024;
        constexpr std::size_t k_TempAllocatorBytes = 10 * 1024 * 1024;

        // 自機の上昇重力と同じ値。下降の -35 は頂点から早く落として操作を返すための値で、操作の無い物には掛けない
        constexpr float k_GravityY = -25.0f;

    } // namespace

    PhysicsScene::RuntimeInit::RuntimeInit()
    {
        detail::InitJoltRuntime();
    }

    JPH::uint PhysicsScene::BPLayerInterface::GetNumBroadPhaseLayers() const
    {
        return BroadPhaseLayers::Count;
    }

    JPH::BroadPhaseLayer PhysicsScene::BPLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
    {
        JPH_ASSERT(layer < ObjectLayers::Count);
        return JPH::BroadPhaseLayer{static_cast<JPH::BroadPhaseLayer::Type>(layer)};
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* PhysicsScene::BroadPhaseLayerInterface::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
    {
        if (layer == BroadPhaseLayers::Terrain)
            return "Terrain";
        if (layer == BroadPhaseLayers::Rock)
            return "Rock";
        if (layer == BroadPhaseLayers::Debris)
            return "Debris";
        if (layer == BroadPhaseLayers::Trigger)
            return "Trigger";
        return "Unknown";
    }
#endif

    bool PhysicsScene::ObjLayerPairFilter::ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const
    {
        if (first == ObjectLayers::Terrain)
        {
            return second == ObjectLayers::Rock || second == ObjectLayers::Debris;
        }
        if (first == ObjectLayers::Rock)
        {
            return second == ObjectLayers::Terrain || second == ObjectLayers::Rock || second == ObjectLayers::Debris;
        }
        if (first == ObjectLayers::Debris)
        {
            return second == ObjectLayers::Terrain || second == ObjectLayers::Rock;
        }

        return false;
    }

    bool PhysicsScene::ObjVsBPLayerFilter::ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const
    {
        const auto other = static_cast<JPH::ObjectLayer>(broadPhase.GetValue());
        if (object == ObjectLayers::Terrain)
        {
            return other == ObjectLayers::Rock || other == ObjectLayers::Debris;
        }
        if (object == ObjectLayers::Rock)
        {
            return other == ObjectLayers::Terrain || other == ObjectLayers::Rock || other == ObjectLayers::Debris;
        }
        if (object == ObjectLayers::Debris)
        {
            return other == ObjectLayers::Terrain || other == ObjectLayers::Rock;
        }

        return false;
    }

    void PhysicsScene::ContactRecorder::OnContactAdded(const JPH::Body& first,
                                                       const JPH::Body& second,
                                                       const JPH::ContactManifold& manifold,
                                                       JPH::ContactSettings& settings)
    {
        (void)settings;
        // 法線は second を押し出す向き。first から見ると逆なので、記録する側で外向きへ揃える
        const JPH::Vec3 outward = manifold.mWorldSpaceNormal;
        m_records.push_back(Record{first.GetID(), BodyContact{second.GetID(), FromJolt(-outward)}});
        m_records.push_back(Record{second.GetID(), BodyContact{first.GetID(), FromJolt(outward)}});
    }

    void PhysicsScene::ContactRecorder::Clear() noexcept
    {
        m_records.clear();
    }

    std::vector<BodyContact> PhysicsScene::ContactRecorder::Of(JPH::BodyID id) const
    {
        std::vector<BodyContact> found;
        for (const Record& record : m_records)
        {
            if (record.owner == id)
            {
                found.push_back(record.contact);
            }
        }
        return found;
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
        m_physicsSystem.SetContactListener(&m_contactRecorder);
        m_physicsSystem.SetGravity(JPH::Vec3{0.0f, k_GravityY, 0.0f});
    }

    PhysicsScene::~PhysicsScene() = default;

    JPH::uint PhysicsScene::BodyCount() const noexcept
    {
        return m_physicsSystem.GetNumBodies();
    }

    JPH::BodyID PhysicsScene::AddStatic(const JPH::ShapeRefC& shape,
                                        const NS::Core::Vector3& position,
                                        const NS::Core::Quaternion& rotation,
                                        JPH::ObjectLayer layer,
                                        bool sensor)
    {
        if (shape == nullptr)
        {
            return JPH::BodyID{};
        }

        JPH::BodyCreationSettings settings{shape, ToJolt(position), ToJolt(rotation), JPH::EMotionType::Static, layer};
        settings.mIsSensor = sensor;

        return m_physicsSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    JPH::BodyID PhysicsScene::SyncStatic(JPH::BodyID id,
                                         const JPH::ShapeRefC& shape,
                                         const NS::Core::Vector3& position,
                                         const NS::Core::Quaternion& rotation,
                                         JPH::ObjectLayer layer,
                                         bool sensor)
    {
        if (shape == nullptr)
        {
            return JPH::BodyID{};
        }

        if (id.IsInvalid())
        {
            return AddStatic(shape, position, rotation, layer, sensor);
        }

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        bodies.SetShape(id, shape, false, JPH::EActivation::DontActivate);
        bodies.SetPositionAndRotationWhenChanged(
            id, ToJolt(position), ToJolt(rotation), JPH::EActivation::DontActivate);
        bodies.SetObjectLayer(id, layer);
        bodies.SetIsSensor(id, sensor);

        return id;
    }

    JPH::BodyID PhysicsScene::AddBox(const NS::Core::OBB& box, JPH::ObjectLayer layer)
    {
        return SyncBox(JPH::BodyID{}, box, layer);
    }

    JPH::BodyID PhysicsScene::SyncBox(JPH::BodyID id, const NS::Core::OBB& box, JPH::ObjectLayer layer, bool sensor)
    {
        const JPH::BoxShapeSettings shapeSettings{JPH::Vec3{box.halfExtentX, box.halfExtentY, box.halfExtentZ}};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
        {
            return JPH::BodyID{};
        }

        const JPH::Mat44 axes{JPH::Vec4{ToJolt(box.axisX), 0.0f},
                              JPH::Vec4{ToJolt(box.axisY), 0.0f},
                              JPH::Vec4{ToJolt(box.axisZ), 0.0f},
                              JPH::Vec4{0.0f, 0.0f, 0.0f, 1.0f}};

        return SyncStatic(id, shape.Get(), box.center, FromJolt(axes.GetQuaternion()), layer, sensor);
    }

    JPH::BodyID PhysicsScene::AddSphere(const NS::Core::Sphere& sphere, JPH::ObjectLayer layer)
    {
        return SyncSphere(JPH::BodyID{}, sphere, layer);
    }

    JPH::BodyID PhysicsScene::SyncSphere(JPH::BodyID id, const NS::Core::Sphere& sphere, JPH::ObjectLayer layer)
    {
        const JPH::SphereShapeSettings shapeSettings{sphere.radius};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
        {
            return JPH::BodyID{};
        }

        return SyncStatic(id, shape.Get(), sphere.center, NS::Core::Quaternion::Identity, layer, false);
    }

    JPH::BodyID PhysicsScene::AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer)
    {
        return SyncCapsule(JPH::BodyID{}, capsule, layer);
    }

    JPH::BodyID PhysicsScene::SyncCapsule(JPH::BodyID id, const Capsule& capsule, JPH::ObjectLayer layer)
    {
        const JPH::CapsuleShapeSettings shapeSettings{capsule.halfHeight, capsule.radius};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
        {
            return JPH::BodyID{};
        }

        const JPH::Quat rotation =
            JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), ToJolt(capsule.axis).NormalizedOr(JPH::Vec3::sAxisY()));
        return SyncStatic(id, shape.Get(), capsule.center, FromJolt(rotation), layer, false);
    }

    JPH::BodyID PhysicsScene::AddMesh(std::span<const Triangle> triangles, JPH::ObjectLayer layer)
    {
        return SyncMesh(JPH::BodyID{}, triangles, layer);
    }

    JPH::BodyID PhysicsScene::SyncMesh(JPH::BodyID id, std::span<const Triangle> triangles, JPH::ObjectLayer layer)
    {
        return SyncStatic(id,
                          CreateMeshShape(triangles),
                          NS::Core::Vector3{0.0f, 0.0f, 0.0f},
                          NS::Core::Quaternion::Identity,
                          layer,
                          false);
    }

    JPH::BodyID PhysicsScene::SyncMeshShape(JPH::BodyID id,
                                            const MeshCollision& collision,
                                            const NS::Core::Vector3& position,
                                            const NS::Core::Quaternion& rotation,
                                            const NS::Core::Vector3& scale,
                                            JPH::ObjectLayer layer)
    {
        if (collision.shape == nullptr)
        {
            return JPH::BodyID{};
        }

        // 拡縮が 1 なら ScaleShape は共有の形そのものを返す
        const JPH::Shape::ShapeResult scaled = collision.shape->ScaleShape(ToJolt(scale));
        if (scaled.HasError())
        {
            return JPH::BodyID{};
        }

        return SyncStatic(id, scaled.Get(), position, rotation, layer, false);
    }

    JPH::BodyID PhysicsScene::AddDynamic(const JPH::ShapeRefC& shape,
                                         const NS::Core::Vector3& position,
                                         const NS::Core::Quaternion& rotation,
                                         const DynamicBodyDesc& desc)
    {
        if (shape == nullptr || shape->MustBeStatic())
        {
            return JPH::BodyID{};
        }

        JPH::BodyCreationSettings settings{
            shape, ToJolt(position), ToJolt(rotation), JPH::EMotionType::Dynamic, desc.layer};
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass > 0.0f ? desc.mass : 1.0f;
        settings.mRestitution = desc.restitution;
        settings.mFriction = desc.friction;
        return m_physicsSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
    }

    JPH::BodyID PhysicsScene::AddSensorBox(const NS::Core::OBB& box)
    {
        return SyncBox(JPH::BodyID{}, box, ObjectLayers::Trigger, true);
    }

    JPH::BodyID PhysicsScene::AddDynamicBox(const NS::Core::OBB& box, const DynamicBodyDesc& desc)
    {
        const JPH::BoxShapeSettings shapeSettings{JPH::Vec3{box.halfExtentX, box.halfExtentY, box.halfExtentZ}};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
        {
            return JPH::BodyID{};
        }

        const JPH::Mat44 axes{JPH::Vec4{ToJolt(box.axisX), 0.0f},
                              JPH::Vec4{ToJolt(box.axisY), 0.0f},
                              JPH::Vec4{ToJolt(box.axisZ), 0.0f},
                              JPH::Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        return AddDynamic(shape.Get(), box.center, FromJolt(axes.GetQuaternion()), desc);
    }

    JPH::BodyID PhysicsScene::AddDynamicSphere(const NS::Core::Sphere& sphere, const DynamicBodyDesc& desc)
    {
        const JPH::SphereShapeSettings shapeSettings{sphere.radius};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
        {
            return JPH::BodyID{};
        }

        return AddDynamic(shape.Get(), sphere.center, NS::Core::Quaternion::Identity, desc);
    }

    void PhysicsScene::OptimizeBroadPhase()
    {
        m_physicsSystem.OptimizeBroadPhase();
    }

    void PhysicsScene::Update(float deltaTime)
    {
        // 前の Update の接触を残すと、離れた後も当たり続けて見える
        m_contactRecorder.Clear();
        // NS の固定更新が 1/60 秒なので分割は 1
        m_physicsSystem.Update(deltaTime, 1, &m_tempAllocator, &m_jobSystem);
    }

    void PhysicsScene::SetBodyAngularVelocity(JPH::BodyID id, const NS::Core::Vector3& angularVelocity)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().SetAngularVelocity(id, ToJolt(angularVelocity));
    }

    NS::Core::Vector3 PhysicsScene::BodyAngularVelocity(JPH::BodyID id) const
    {
        return FromJolt(m_physicsSystem.GetBodyInterfaceNoLock().GetAngularVelocity(id));
    }

    bool PhysicsScene::IsBodyAwake(JPH::BodyID id) const
    {
        if (id.IsInvalid())
        {
            return false;
        }

        return m_physicsSystem.GetBodyInterfaceNoLock().IsActive(id);
    }

    std::vector<BodyContact> PhysicsScene::ContactsOf(JPH::BodyID id) const
    {
        if (id.IsInvalid())
        {
            return {};
        }

        return m_contactRecorder.Of(id);
    }

    void PhysicsScene::RemoveBody(JPH::BodyID id)
    {
        if (id.IsInvalid())
        {
            return;
        }

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }

    bool PhysicsScene::Raycast(const NS::Core::Vector3& origin,
                               const NS::Core::Vector3& direction,
                               float maxDistance,
                               float& outDistance) const
    {
        const float directionLength = direction.Length();
        if (!(maxDistance > 0.0f) || !(directionLength > 0.0f))
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

    std::vector<JPH::BodyID> PhysicsScene::OverlapCapsule(const Capsule& capsule) const
    {
        const JPH::CapsuleShapeSettings shapeSettings{capsule.halfHeight, capsule.radius};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
        {
            return {};
        }

        // JPH::CapsuleShape は Y 軸に沿った形なので、Y から axis へ回す
        const JPH::Vec3 axis = ToJolt(capsule.axis).NormalizedOr(JPH::Vec3::sAxisY());
        const JPH::Quat rotation = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis);
        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(rotation, ToJolt(capsule.center));

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        m_physicsSystem.GetNarrowPhaseQueryNoLock().CollideShape(
            shape.Get(), JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings{}, JPH::RVec3::sZero(), collector);

        // mesh の body は三角形ごとに当たりを返すので、同じ id を落としてから返す
        std::vector<JPH::BodyID> found;
        found.reserve(collector.mHits.size());
        for (const JPH::CollideShapeResult& hit : collector.mHits)
        {
            if (std::find(found.begin(), found.end(), hit.mBodyID2) == found.end())
            {
                found.push_back(hit.mBodyID2);
            }
        }
        return found;
    }

    std::vector<NS::Core::AABB> PhysicsScene::OverlapBox(const NS::Core::AABB& region) const
    {
        const JPH::Vec3 center = ToJolt(region.Center);
        const JPH::Vec3 extents = ToJolt(region.Extents);
        const JPH::AABox box{center - extents, center + extents};

        JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
        m_physicsSystem.GetBroadPhaseQuery().CollideAABox(box, collector);

        std::vector<NS::Core::AABB> found;
        found.reserve(collector.mHits.size());
        const JPH::BodyLockInterface& lock = m_physicsSystem.GetBodyLockInterfaceNoLock();
        for (const JPH::BodyID id : collector.mHits)
        {
            const JPH::BodyLockRead body{lock, id};
            if (!body.Succeeded())
            {
                continue;
            }

            const JPH::AABox& bounds = body.GetBody().GetWorldSpaceBounds();
            NS::Core::AABB out;
            out.Center = FromJolt(bounds.GetCenter());
            out.Extents = FromJolt(bounds.GetExtent());
            found.push_back(out);
        }
        return found;
    }

    void PhysicsScene::SetBodyVelocity(JPH::BodyID id, const NS::Core::Vector3& velocity)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().SetLinearVelocity(id, ToJolt(velocity));
    }

    NS::Core::Vector3 PhysicsScene::BodyVelocity(JPH::BodyID id) const
    {
        return FromJolt(m_physicsSystem.GetBodyInterfaceNoLock().GetLinearVelocity(id));
    }

    NS::Core::Vector3 PhysicsScene::BodyPosition(JPH::BodyID id) const
    {
        return FromJolt(m_physicsSystem.GetBodyInterfaceNoLock().GetPosition(id));
    }

    NS::Core::Quaternion PhysicsScene::BodyRotation(JPH::BodyID id) const
    {
        return FromJolt(m_physicsSystem.GetBodyInterfaceNoLock().GetRotation(id));
    }
} // namespace NS::Physics
