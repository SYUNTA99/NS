#include "Runtime/Physics/JoltWorld.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Physics/JoltConversion.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>

#include <memory>
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

        class JoltRuntime
        {
        public:
            JoltRuntime()
            {
                JPH::RegisterDefaultAllocator();
                m_factory = std::make_unique<JPH::Factory>();
                JPH::Factory::sInstance = m_factory.get();
                JPH::RegisterTypes();
            }

            ~JoltRuntime()
            {
                JPH::UnregisterTypes();
                JPH::Factory::sInstance = nullptr;
            }

        private:
            std::unique_ptr<JPH::Factory> m_factory;
        };

        void InitializeJoltRuntime()
        {
            static JoltRuntime runtime;
        }

    } // namespace

    JoltWorld::RuntimeInitialization::RuntimeInitialization()
    {
        InitializeJoltRuntime();
    }

    JPH::uint JoltWorld::BroadPhaseLayerInterface::GetNumBroadPhaseLayers() const
    {
        return BroadPhaseLayers::Count;
    }

    JPH::BroadPhaseLayer JoltWorld::BroadPhaseLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
    {
        JPH_ASSERT(layer < ObjectLayers::Count);
        return JPH::BroadPhaseLayer{static_cast<JPH::BroadPhaseLayer::Type>(layer)};
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* JoltWorld::BroadPhaseLayerInterface::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
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

    bool JoltWorld::ObjectLayerPairFilter::ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const
    {
        if (first == ObjectLayers::Terrain)
            return second == ObjectLayers::Rock || second == ObjectLayers::Debris;
        if (first == ObjectLayers::Rock)
            return second == ObjectLayers::Terrain || second == ObjectLayers::Rock || second == ObjectLayers::Debris;
        if (first == ObjectLayers::Debris)
            return second == ObjectLayers::Terrain || second == ObjectLayers::Rock;
        return false;
    }

    bool JoltWorld::ObjectVsBroadPhaseLayerFilter::ShouldCollide(JPH::ObjectLayer object,
                                                                 JPH::BroadPhaseLayer broadPhase) const
    {
        const auto other = static_cast<JPH::ObjectLayer>(broadPhase.GetValue());
        if (object == ObjectLayers::Terrain)
            return other == ObjectLayers::Rock || other == ObjectLayers::Debris;
        if (object == ObjectLayers::Rock)
            return other == ObjectLayers::Terrain || other == ObjectLayers::Rock || other == ObjectLayers::Debris;
        if (object == ObjectLayers::Debris)
            return other == ObjectLayers::Terrain || other == ObjectLayers::Rock;
        return false;
    }

    JoltWorld::JoltWorld() : m_tempAllocator(k_TempAllocatorBytes), m_jobSystem(k_MaxJobs)
    {
        m_physicsSystem.Init(k_MaxBodies,
                             0,
                             k_MaxBodyPairs,
                             k_MaxContactConstraints,
                             m_broadPhaseLayerInterface,
                             m_objectVsBroadPhaseLayerFilter,
                             m_objectLayerPairFilter);
    }

    JoltWorld::~JoltWorld() = default;

    JPH::uint JoltWorld::BodyCount() const noexcept
    {
        return m_physicsSystem.GetNumBodies();
    }

    JPH::BodyID JoltWorld::AddStatic(const JPH::ShapeRefC& shape,
                                     const NS::Core::Vector3& position,
                                     const NS::Core::Quaternion& rotation,
                                     JPH::ObjectLayer layer)
    {
        if (shape == nullptr)
            return JPH::BodyID{};

        JPH::BodyCreationSettings settings{shape, ToJolt(position), ToJolt(rotation), JPH::EMotionType::Static, layer};
        // false のまま作った body は Dynamic への SetMotionType が JPH_ASSERT で止まる
        // 動かすかを決めるのは SetBodyDynamic を呼ぶ側で、Add 系にそれを伝える引数は無い
        // mesh は体積を出せず質量が 0 になる。true にすると body の生成が JPH_ASSERT で止まる
        settings.mAllowDynamicOrKinematic = !shape->MustBeStatic();
        return m_physicsSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    JPH::BodyID JoltWorld::AddBox(const NS::Core::OBB& box, JPH::ObjectLayer layer)
    {
        const JPH::BoxShapeSettings shapeSettings{JPH::Vec3{box.halfExtentX, box.halfExtentY, box.halfExtentZ}};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
            return JPH::BodyID{};

        const JPH::Mat44 axes{JPH::Vec4{ToJolt(box.axisX), 0.0f},
                              JPH::Vec4{ToJolt(box.axisY), 0.0f},
                              JPH::Vec4{ToJolt(box.axisZ), 0.0f},
                              JPH::Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        return AddStatic(shape.Get(), box.center, FromJolt(axes.GetQuaternion()), layer);
    }

    JPH::BodyID JoltWorld::AddSphere(const NS::Core::Sphere& sphere, JPH::ObjectLayer layer)
    {
        const JPH::SphereShapeSettings shapeSettings{sphere.radius};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
            return JPH::BodyID{};

        return AddStatic(shape.Get(), sphere.center, NS::Core::Quaternion::Identity, layer);
    }

    JPH::BodyID JoltWorld::AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer)
    {
        const JPH::CapsuleShapeSettings shapeSettings{capsule.halfHeight, capsule.radius};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
            return JPH::BodyID{};

        const JPH::Quat rotation =
            JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), ToJolt(capsule.axis).NormalizedOr(JPH::Vec3::sAxisY()));
        return AddStatic(shape.Get(), capsule.center, FromJolt(rotation), layer);
    }

    JPH::BodyID JoltWorld::AddMesh(std::span<const Triangle> triangles, JPH::ObjectLayer layer)
    {
        if (triangles.empty())
            return JPH::BodyID{};

        JPH::TriangleList list;
        list.reserve(triangles.size());
        for (const Triangle& triangle : triangles)
        {
            list.push_back(JPH::Triangle{JPH::Float3{triangle.v0.x, triangle.v0.y, triangle.v0.z},
                                         JPH::Float3{triangle.v1.x, triangle.v1.y, triangle.v1.z},
                                         JPH::Float3{triangle.v2.x, triangle.v2.y, triangle.v2.z}});
        }

        const JPH::MeshShapeSettings shapeSettings{std::move(list)};
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError())
            return JPH::BodyID{};

        return AddStatic(shape.Get(), NS::Core::Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Quaternion::Identity, layer);
    }

    void JoltWorld::OptimizeBroadPhase()
    {
        m_physicsSystem.OptimizeBroadPhase();
    }

    void JoltWorld::Update(float deltaTime)
    {
        // NS の固定更新が 1/60 秒なので分割は 1
        m_physicsSystem.Update(deltaTime, 1, &m_tempAllocator, &m_jobSystem);
    }

    void JoltWorld::SetBodyDynamic(JPH::BodyID id, bool dynamic)
    {
        if (id.IsInvalid())
            return;

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        // 静的専用の形の body には MotionProperties が無く、Dynamic を渡すと Jolt の JPH_ASSERT で落ちる
        if (dynamic && bodies.GetShape(id)->MustBeStatic())
        {
            NS_LOG_WARN(Physics, "静的専用の形なので動的にできない");
            return;
        }

        bodies.SetMotionType(id,
                             dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                             dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    }

    void JoltWorld::RemoveBody(JPH::BodyID id)
    {
        if (id.IsInvalid())
            return;

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }

    void JoltWorld::RemoveAllBodies()
    {
        JPH::BodyIDVector ids;
        m_physicsSystem.GetBodies(ids);
        if (ids.empty())
            return;

        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        bodies.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
        bodies.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
    }

    NS::Core::Vector3 JoltWorld::BodyPosition(JPH::BodyID id) const
    {
        return FromJolt(m_physicsSystem.GetBodyInterfaceNoLock().GetPosition(id));
    }

    NS::Core::Quaternion JoltWorld::BodyRotation(JPH::BodyID id) const
    {
        return FromJolt(m_physicsSystem.GetBodyInterfaceNoLock().GetRotation(id));
    }
} // namespace NS::Physics
