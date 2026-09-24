#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Physics/detail/JoltConversion.h"
#include "Runtime/Physics/detail/JoltRuntime.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace NS::Phys
{
    namespace
    {
        constexpr JPH::uint k_MaxBodies = 4096;
        constexpr JPH::uint k_MaxBodyPairs = 16384;
        constexpr JPH::uint k_MaxContactConstraints = 4096;
        constexpr JPH::uint k_MaxJobs = 1024;
        constexpr std::size_t k_TempAllocatorBytes = 10 * 1024 * 1024;

        [[nodiscard]] float FiniteOr(float value, float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        // Jolt は負の減衰と 0 以下の質量で assert する。受け取る所で揃えて、呼出側に同じ確かめを書かせない
        [[nodiscard]] BodyMotion Sanitized(const BodyMotion& motion) noexcept
        {
            const BodyMotion defaults;
            BodyMotion out = motion;
            out.mass = FiniteOr(motion.mass, defaults.mass);
            if (!(out.mass > 0.0f))
            {
                out.mass = defaults.mass;
            }
            out.friction = std::max(0.0f, FiniteOr(motion.friction, defaults.friction));
            out.restitution = std::max(0.0f, FiniteOr(motion.restitution, defaults.restitution));
            out.linearDamping = std::max(0.0f, FiniteOr(motion.linearDamping, defaults.linearDamping));
            out.angularDamping = std::max(0.0f, FiniteOr(motion.angularDamping, defaults.angularDamping));
            out.gravityFactor = FiniteOr(motion.gravityFactor, defaults.gravityFactor);
            return out;
        }

        // 軸を全部塞いだ動的 body は Jolt が 0 で割って落ちる。力で動かせないので、キネマティックとして作る
        [[nodiscard]] JPH::EMotionType MotionTypeOf(const BodyMotion& motion) noexcept
        {
            if (motion.kinematic || motion.allowedDOFs == JPH::EAllowedDOFs::None)
            {
                return JPH::EMotionType::Kinematic;
            }
            return JPH::EMotionType::Dynamic;
        }

        [[nodiscard]] JPH::EAllowedDOFs AllowedDOFsOf(const BodyMotion& motion) noexcept
        {
            if (MotionTypeOf(motion) == JPH::EMotionType::Kinematic)
            {
                return JPH::EAllowedDOFs::All;
            }
            return motion.allowedDOFs;
        }

        [[nodiscard]] JPH::EMotionQuality MotionQualityOf(const BodyMotion& motion) noexcept
        {
            return motion.continuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        }

        // 形の組を body の原点から見た 1 つの形にする。部品が残らないか、作れなければ null
        [[nodiscard]] JPH::ShapeRefC BuildMovingShape(std::span<const ShapePart> parts,
                                                      JPH::Vec3Arg origin,
                                                      JPH::QuatArg rotation)
        {
            const JPH::Quat toLocal = rotation.Conjugated();

            JPH::StaticCompoundShapeSettings compound;
            JPH::ShapeRefC single;
            JPH::Vec3 singlePosition = JPH::Vec3::sZero();
            JPH::Quat singleRotation = JPH::Quat::sIdentity();
            std::size_t count = 0;
            for (const ShapePart& part : parts)
            {
                // 三角形の形は動く body に入れられない。1 つでも混ぜると合成形状ごと静的専用になる
                if (part.shape == nullptr || part.shape->MustBeStatic())
                {
                    continue;
                }

                const JPH::Vec3 localPosition = toLocal * (ToJolt(part.position) - origin);
                const JPH::Quat localRotation = (toLocal * ToJolt(part.rotation).Normalized()).Normalized();
                compound.AddShape(localPosition, localRotation, part.shape.GetPtr());
                single = part.shape;
                singlePosition = localPosition;
                singleRotation = localRotation;
                ++count;
            }

            if (count == 0)
            {
                return nullptr;
            }

            // Jolt の合成形状は部品を 2 つ以上要る。1 つならずらした形で包む
            if (count == 1)
            {
                // body の原点にぴったり重なっていれば包まずにそのまま使う
                if (singlePosition.IsNearZero() && singleRotation.IsClose(JPH::Quat::sIdentity()))
                {
                    return single;
                }
                const JPH::RotatedTranslatedShapeSettings shifted{singlePosition, singleRotation, single.GetPtr()};
                const JPH::ShapeSettings::ShapeResult result = shifted.Create();
                return result.HasError() ? nullptr : result.Get();
            }

            const JPH::ShapeSettings::ShapeResult result = compound.Create();
            return result.HasError() ? nullptr : result.Get();
        }
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
        m_physicsSystem.SetGravity(ToJolt(DefaultGravity()));
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
        const ShapePart part = MakeBoxPart(box);
        return SyncStatic(id, part.shape, part.position, part.rotation, layer, sensor);
    }

    JPH::BodyID PhysicsScene::AddSphere(const NS::Core::Sphere& sphere, JPH::ObjectLayer layer)
    {
        return SyncSphere(JPH::BodyID{}, sphere, layer);
    }

    JPH::BodyID PhysicsScene::SyncSphere(JPH::BodyID id, const NS::Core::Sphere& sphere, JPH::ObjectLayer layer)
    {
        const ShapePart part = MakeSpherePart(sphere);
        return SyncStatic(id, part.shape, part.position, part.rotation, layer, false);
    }

    JPH::BodyID PhysicsScene::AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer)
    {
        return SyncCapsule(JPH::BodyID{}, capsule, layer);
    }

    JPH::BodyID PhysicsScene::SyncCapsule(JPH::BodyID id, const Capsule& capsule, JPH::ObjectLayer layer)
    {
        const ShapePart part = MakeCapsulePart(capsule);
        return SyncStatic(id, part.shape, part.position, part.rotation, layer, false);
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
        const ShapePart part = MakeBoxPart(box);
        return AddDynamic(part.shape, part.position, part.rotation, desc);
    }

    JPH::BodyID PhysicsScene::AddDynamicSphere(const NS::Core::Sphere& sphere, const DynamicBodyDesc& desc)
    {
        const ShapePart part = MakeSpherePart(sphere);
        return AddDynamic(part.shape, part.position, part.rotation, desc);
    }

    JPH::BodyID PhysicsScene::SyncMovingBody(JPH::BodyID id,
                                             std::span<const ShapePart> parts,
                                             const NS::Core::Vector3& position,
                                             const NS::Core::Quaternion& rotation,
                                             const BodyMotion& motion,
                                             JPH::ObjectLayer layer)
    {
        const JPH::Vec3 origin = ToJolt(position);
        const JPH::Quat orientation = ToJolt(rotation).Normalized();
        const JPH::ShapeRefC shape = BuildMovingShape(parts, origin, orientation);
        if (shape == nullptr)
        {
            return JPH::BodyID{};
        }

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        // 静的な body は動き方を持たないので、動く body へ切り替えられない。作り直す
        if (!id.IsInvalid() && bodies.GetMotionType(id) != JPH::EMotionType::Static)
        {
            bodies.SetShape(id, shape, false, JPH::EActivation::DontActivate);
            bodies.SetPositionAndRotation(id, origin, orientation, JPH::EActivation::DontActivate);
            bodies.SetObjectLayer(id, layer);
            // 形を替えた後に質量を形から計り直す。SetShape に計らせると上書きした質量が消える
            SetBodyMotion(id, motion);
            return id;
        }

        const BodyMotion safe = Sanitized(motion);
        JPH::BodyCreationSettings settings{shape, origin, orientation, MotionTypeOf(safe), layer};
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = safe.mass;
        settings.mFriction = safe.friction;
        settings.mRestitution = safe.restitution;
        settings.mLinearDamping = safe.linearDamping;
        settings.mAngularDamping = safe.angularDamping;
        settings.mGravityFactor = safe.gravityFactor;
        settings.mMotionQuality = MotionQualityOf(safe);
        settings.mAllowedDOFs = AllowedDOFsOf(safe);
        return bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }

    void PhysicsScene::SetBodyMotion(JPH::BodyID id, const BodyMotion& motion)
    {
        if (id.IsInvalid())
        {
            return;
        }

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        if (bodies.GetMotionType(id) == JPH::EMotionType::Static)
        {
            return;
        }

        const BodyMotion safe = Sanitized(motion);
        bodies.SetMotionType(id, MotionTypeOf(safe), JPH::EActivation::DontActivate);
        bodies.SetFriction(id, safe.friction);
        bodies.SetRestitution(id, safe.restitution);
        bodies.SetGravityFactor(id, safe.gravityFactor);
        bodies.SetMotionQuality(id, MotionQualityOf(safe));
        {
            // 減衰と質量は BodyInterface に口が無い。ロックは BodyInterface を呼ぶ前に外す
            const JPH::BodyLockWrite lock{m_physicsSystem.GetBodyLockInterface(), id};
            if (lock.Succeeded())
            {
                JPH::Body& body = lock.GetBody();
                JPH::MotionProperties* properties = body.GetMotionProperties();
                properties->SetLinearDamping(safe.linearDamping);
                properties->SetAngularDamping(safe.angularDamping);
                JPH::MassProperties mass = body.GetShape()->GetMassProperties();
                mass.ScaleToMass(safe.mass);
                properties->SetMassProperties(AllowedDOFsOf(safe), mass);
            }
        }
        bodies.ActivateBody(id);
    }

    void PhysicsScene::MoveKinematic(JPH::BodyID id,
                                     const NS::Core::Vector3& position,
                                     const NS::Core::Quaternion& rotation,
                                     float deltaTime)
    {
        if (id.IsInvalid() || !(deltaTime > 0.0f))
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().MoveKinematic(
            id, ToJolt(position), ToJolt(rotation).Normalized(), deltaTime);
    }

    void PhysicsScene::TeleportBody(JPH::BodyID id,
                                    const NS::Core::Vector3& position,
                                    const NS::Core::Quaternion& rotation)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().SetPositionAndRotation(
            id, ToJolt(position), ToJolt(rotation).Normalized(), JPH::EActivation::Activate);
    }

    void PhysicsScene::AddBodyForce(JPH::BodyID id, const NS::Core::Vector3& force)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().AddForce(id, ToJolt(force));
    }

    void PhysicsScene::AddBodyImpulse(JPH::BodyID id, const NS::Core::Vector3& impulse)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().AddImpulse(id, ToJolt(impulse));
    }

    void PhysicsScene::AddBodyTorque(JPH::BodyID id, const NS::Core::Vector3& torque)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().AddTorque(id, ToJolt(torque));
    }

    void PhysicsScene::AddBodyAngularImpulse(JPH::BodyID id, const NS::Core::Vector3& angularImpulse)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().AddAngularImpulse(id, ToJolt(angularImpulse));
    }

    void PhysicsScene::AddBodyAcceleration(JPH::BodyID id, const NS::Core::Vector3& acceleration)
    {
        if (id.IsInvalid() || !std::isfinite(acceleration.x) || !std::isfinite(acceleration.y) ||
            !std::isfinite(acceleration.z))
        {
            return;
        }

        const JPH::BodyLockWrite lock{m_physicsSystem.GetBodyLockInterface(), id};
        if (!lock.Succeeded())
        {
            return;
        }
        JPH::Body& body = lock.GetBody();
        if (!body.IsDynamic() || !body.IsActive())
        {
            return;
        }
        // 移動の軸を全部止めた body は逆質量が 0 で、力を掛けても動かない
        const float inverseMass = body.GetMotionProperties()->GetInverseMass();
        if (!(inverseMass > 0.0f))
        {
            return;
        }
        body.AddForce(ToJolt(acceleration) / inverseMass);
    }

    void PhysicsScene::WakeBody(JPH::BodyID id)
    {
        if (id.IsInvalid())
        {
            return;
        }

        m_physicsSystem.GetBodyInterface().ActivateBody(id);
    }

    void PhysicsScene::SetGravity(const NS::Core::Vector3& gravity)
    {
        if (!std::isfinite(gravity.x) || !std::isfinite(gravity.y) || !std::isfinite(gravity.z))
        {
            return;
        }

        m_physicsSystem.SetGravity(ToJolt(gravity));
    }

    NS::Core::Vector3 PhysicsScene::Gravity() const
    {
        return FromJolt(m_physicsSystem.GetGravity());
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
} // namespace NS::Phys
