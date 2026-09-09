#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/SweptTriangle.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <span>

namespace NS::Physics
{
    //! body の種別を表す ObjectLayer。どの組み合わせが当たるかは 2 つの ShouldCollide が同じ形で持つ
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
        //! @brief ObjectLayer と同じ番号の BroadPhaseLayer を作る
        //! @details BroadPhaseLayerInterface は ObjectLayer をそのまま番号へ cast して割り当てる
        //! 定数側にも番号を書くと、ObjectLayers を並べ替えた時に片方だけ古い番号が残る
        constexpr JPH::BroadPhaseLayer FromObjectLayer(JPH::ObjectLayer layer) noexcept
        {
            return JPH::BroadPhaseLayer{static_cast<JPH::BroadPhaseLayer::Type>(layer)};
        }

        inline constexpr JPH::BroadPhaseLayer Terrain = FromObjectLayer(ObjectLayers::Terrain);
        inline constexpr JPH::BroadPhaseLayer Rock = FromObjectLayer(ObjectLayers::Rock);
        inline constexpr JPH::BroadPhaseLayer Debris = FromObjectLayer(ObjectLayers::Debris);
        inline constexpr JPH::BroadPhaseLayer Trigger = FromObjectLayer(ObjectLayers::Trigger);
        inline constexpr JPH::uint Count = ObjectLayers::Count;
    } // namespace BroadPhaseLayers

    //! @brief JPH::PhysicsSystem と、一時 allocator・job system・layer filter を同じ寿命で持つ衝突 world
    //! @details 最初の 1 個の構築で JPH::RegisterDefaultAllocator / JPH::Factory / JPH::RegisterTypes を 1 度だけ通す
    //! 型の登録解除はプロセス終了時
    //! Add 系はどれも静的 body を 1 つ作り、shape を作れなければ無効な BodyID を返す
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

        //! OBB の中心と 3 軸をそのまま box body にする
        JPH::BodyID AddBox(const NS::Core::OBB& box, JPH::ObjectLayer layer);
        //! 中心と半径をそのまま球 body にする
        JPH::BodyID AddSphere(const NS::Core::Sphere& sphere, JPH::ObjectLayer layer);
        //! capsule body を capsule.axis の向きで入れる。軸が零ベクトルなら Y 軸
        JPH::BodyID AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer);
        //! @brief 三角形群をまとめて 1 つの mesh body にする。空なら作らない
        //! @details 呼出側が std::vector と std::array<Triangle, 8> のどちらでも写さずに渡せるよう span で受ける
        JPH::BodyID AddMesh(std::span<const Triangle> triangles, JPH::ObjectLayer layer);

        //! broadphase の木を組み直す。Add 完了後に 1 度呼ぶ
        void OptimizeBroadPhase();

        //! world を deltaTime 秒ぶん進める。衝突の分割は 1 で、渡した時間を刻まない
        void Update(float deltaTime);

        //! @brief body を dynamic と static で切り替える
        //! @details dynamic にする時だけ body を起こす。無効な BodyID は何もしない
        //! 静的専用の形の body は dynamic にできない。警告を出して戻る
        void SetBodyDynamic(JPH::BodyID id, bool dynamic);

        //! body を world から外して壊す。無効な BodyID は何もしない
        void RemoveBody(JPH::BodyID id);
        //! world の body を全部外して壊す
        void RemoveAllBodies();

        //! body の world 位置
        [[nodiscard]] NS::Core::Vector3 BodyPosition(JPH::BodyID id) const;
        //! body の world 回転
        [[nodiscard]] NS::Core::Quaternion BodyRotation(JPH::BodyID id) const;

    private:
        JPH::BodyID AddStatic(const JPH::ShapeRefC& shape,
                              const NS::Core::Vector3& position,
                              const NS::Core::Quaternion& rotation,
                              JPH::ObjectLayer layer);

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

        // m_tempAllocator より前に置く。構築が呼ぶ Jolt の確保関数は RegisterDefaultAllocator まで nullptr
        RuntimeInitialization m_runtimeInitialization;
        BroadPhaseLayerInterface m_broadPhaseLayerInterface;
        ObjectLayerPairFilter m_objectLayerPairFilter;
        ObjectVsBroadPhaseLayerFilter m_objectVsBroadPhaseLayerFilter;
        JPH::TempAllocatorImpl m_tempAllocator;
        JPH::JobSystemSingleThreaded m_jobSystem;
        JPH::PhysicsSystem m_physicsSystem;
    };
} // namespace NS::Physics
