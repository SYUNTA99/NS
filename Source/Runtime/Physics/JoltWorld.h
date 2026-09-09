#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace NS::Physics
{
    namespace ObjectLayers
    {
        inline constexpr JPH::ObjectLayer Terrain = 0;
        inline constexpr JPH::ObjectLayer Rock = 1;
        inline constexpr JPH::ObjectLayer Debris = 2;
        inline constexpr JPH::ObjectLayer Trigger = 3;
        inline constexpr JPH::uint Count = 4;
    } // namespace ObjectLayers

    namespace BroadPhaseLayers
    {
        inline constexpr JPH::BroadPhaseLayer Terrain{0};
        inline constexpr JPH::BroadPhaseLayer Rock{1};
        inline constexpr JPH::BroadPhaseLayer Debris{2};
        inline constexpr JPH::BroadPhaseLayer Trigger{3};
        inline constexpr JPH::uint Count = 4;
    } // namespace BroadPhaseLayers

    //! @brief JPH::PhysicsSystem と、一時 allocator・job system・layer filter を同じ寿命で持つ衝突 world
    //! @details 最初の 1 個の構築で JPH::RegisterDefaultAllocator / JPH::Factory / JPH::RegisterTypes を 1 度だけ通す
    //! 型の登録解除はプロセス終了時
    class JoltWorld
    {
    public:
        JoltWorld();
        ~JoltWorld();

        JoltWorld(const JoltWorld&) = delete;
        JoltWorld& operator=(const JoltWorld&) = delete;
        JoltWorld(JoltWorld&&) = delete;
        JoltWorld& operator=(JoltWorld&&) = delete;

        //! world に入っている body の数
        [[nodiscard]] JPH::uint BodyCount() const noexcept;

    private:
        class RuntimeInitialization
        {
        public:
            RuntimeInitialization();
        };

        class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
        {
        public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override;
            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
        };

        class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override;
        };

        class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const override;
        };

        RuntimeInitialization m_runtimeInitialization;
        BroadPhaseLayerInterface m_broadPhaseLayerInterface;
        ObjectLayerPairFilter m_objectLayerPairFilter;
        ObjectVsBroadPhaseLayerFilter m_objectVsBroadPhaseLayerFilter;
        JPH::TempAllocatorImpl m_tempAllocator;
        JPH::JobSystemSingleThreaded m_jobSystem;
        JPH::PhysicsSystem m_physicsSystem;
    };
} // namespace NS::Physics
