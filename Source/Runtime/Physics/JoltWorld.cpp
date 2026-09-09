#include "Runtime/Physics/JoltWorld.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <memory>

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
} // namespace NS::Physics
