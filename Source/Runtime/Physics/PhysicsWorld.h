#pragma once

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/Triangle.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <span>
#include <vector>

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

    //! @brief 1 歩の間に記録した接触 1 件
    struct BodyContact
    {
        JPH::BodyID other;        // ぶつかった相手の body
        NS::Core::Vector3 normal; // 接触面の法線。持ち主を相手から離す向き
    };

    //! @brief 動的 body を作る時の設定
    struct DynamicBodyDesc
    {
        JPH::ObjectLayer layer = ObjectLayers::Rock; // 当たる相手を決める種別
        float mass = 1.0f;                           // 質量。0 以下なら 1 として作る
        float restitution = 0.0f;                    // 跳ね返り
        float friction = 0.2f;                       // 摩擦
    };

    //! @brief JPH::PhysicsSystem と、一時 allocator・job system・layer filter を同じ寿命で持つ衝突 world
    //! @details 最初の 1 個の構築で JPH::RegisterDefaultAllocator / JPH::Factory / JPH::RegisterTypes を 1 度だけ通す
    //! 型の登録解除はプロセス終了時
    //! Add 系はどれも body を 1 つ作り、shape を作れなければ無効な BodyID を返す
    //! 作った時点で動的なのは AddDynamic の付く 2 つだけで、これだけが起きた状態で入る
    class PhysicsWorld
    {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) = delete;
        PhysicsWorld& operator=(PhysicsWorld&&) = delete;

        //! world に入っている body の数
        [[nodiscard]] JPH::uint BodyCount() const noexcept;

        //! OBB の中心と 3 軸をそのまま box body にする
        JPH::BodyID AddBox(const NS::Core::OBB& box, JPH::ObjectLayer layer);
        //! id の body を box の形と姿勢・layer・sensor の有無へ書き換えて id を返す。id が無効なら新しく作る
        JPH::BodyID SyncBox(JPH::BodyID id, const NS::Core::OBB& box, JPH::ObjectLayer layer, bool sensor = false);
        //! 中心と半径をそのまま球 body にする
        JPH::BodyID AddSphere(const NS::Core::Sphere& sphere, JPH::ObjectLayer layer);
        //! id の body を sphere の形と位置・layer へ書き換えて id を返す。id が無効なら新しく作る
        JPH::BodyID SyncSphere(JPH::BodyID id, const NS::Core::Sphere& sphere, JPH::ObjectLayer layer);
        //! capsule body を capsule.axis の向きで入れる。軸が零ベクトルなら Y 軸
        JPH::BodyID AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer);
        //! id の body を capsule の形と姿勢・layer へ書き換えて id を返す。id が無効なら新しく作る
        JPH::BodyID SyncCapsule(JPH::BodyID id, const Capsule& capsule, JPH::ObjectLayer layer);
        //! @brief 三角形群をまとめて 1 つの mesh body にする。空なら作らない
        //! @details 呼出側が std::vector と std::array<Triangle, 8> のどちらでも写さずに渡せるよう span で受ける
        JPH::BodyID AddMesh(std::span<const Triangle> triangles, JPH::ObjectLayer layer);
        //! id の body を三角形群の形と layer へ書き換えて id を返す。id が無効なら新しく作る
        //! 空なら無効な BodyID を返す
        JPH::BodyID SyncMesh(JPH::BodyID id, std::span<const Triangle> triangles, JPH::ObjectLayer layer);

        //! @brief OBB を通り抜けられる sensor body にする
        //! @details layer は ObjectLayers::Trigger 固定で、2 つの ShouldCollide がどの layer とも組ませない
        //! 押し戻しも接触の通知も起きず、出てくるのは layer で絞らない RaycastDown・OverlapCapsule・OverlapBox だけ
        JPH::BodyID AddSensorBox(const NS::Core::OBB& box);

        //! OBB の中心と 3 軸をそのまま動的な box body にする
        JPH::BodyID AddDynamicBox(const NS::Core::OBB& box, const DynamicBodyDesc& desc);
        //! 中心と半径をそのまま動的な球 body にする
        JPH::BodyID AddDynamicSphere(const NS::Core::Sphere& sphere, const DynamicBodyDesc& desc);

        //! body の角速度を置く。無効な BodyID は何もしない
        void SetBodyAngularVelocity(JPH::BodyID id, const NS::Core::Vector3& angularVelocity);
        //! body の角速度
        [[nodiscard]] NS::Core::Vector3 BodyAngularVelocity(JPH::BodyID id) const;
        //! body が起きている場合 true、それ以外の場合は false。無効な BodyID は false
        [[nodiscard]] bool IsBodyAwake(JPH::BodyID id) const;
        //! 直近の Update で記録した id の接触。前の歩の分は残らない。無効な BodyID は空
        [[nodiscard]] std::vector<BodyContact> ContactsOf(JPH::BodyID id) const;

        //! broadphase の木を組み直す。Add 完了後に 1 度呼ぶ
        void OptimizeBroadPhase();

        //! world を deltaTime 秒ぶん進める。衝突の分割は 1 で、渡した時間を刻まない
        void Update(float deltaTime);

        //! @brief body を dynamic と static で切り替える
        //! @details dynamic にする時だけ body を起こす。無効な BodyID は何もしない
        //! 静的専用の形の body は dynamic にできない。警告を出して戻る
        void SetBodyDynamic(JPH::BodyID id, bool dynamic);

        //! @brief origin から真下へ maxDistance までの間で最も近い命中までの距離を outDistance に返す
        //! @details layer でも shape でも絞らないので、world の全 body が対象
        //! maxDistance が正でなければ false。命中が無ければ outDistance を変えない
        [[nodiscard]] bool RaycastDown(const NS::Core::Vector3& origin, float maxDistance, float& outDistance) const;

        //! @brief capsule に重なっている body の id を集めて返す
        //! @details 形の実物どうしで見るので、回転した box は外接箱ではなく本当の形で判定する
        //! sensor も layer も問わない。同じ body は 1 度だけ返る
        [[nodiscard]] std::vector<JPH::BodyID> OverlapCapsule(const Capsule& capsule) const;

        //! @brief region に重なる body の world 空間の境界箱を集めて返す
        //! @details 重なりを見るのも返すのも軸並行の境界箱で、shape の形は見ない
        //! 回転した box や mesh では形より大きい箱が返る
        [[nodiscard]] std::vector<NS::Core::AABB> OverlapBox(const NS::Core::AABB& region) const;

        //! body の線速度を置く。無効な BodyID は何もしない
        void SetBodyVelocity(JPH::BodyID id, const NS::Core::Vector3& velocity);

        //! body の線速度
        [[nodiscard]] NS::Core::Vector3 BodyVelocity(JPH::BodyID id) const;

        //! body を world から外して壊す。無効な BodyID は何もしない
        void RemoveBody(JPH::BodyID id);

        //! body の world 位置
        [[nodiscard]] NS::Core::Vector3 BodyPosition(JPH::BodyID id) const;
        //! body の world 回転
        [[nodiscard]] NS::Core::Quaternion BodyRotation(JPH::BodyID id) const;

    private:
        friend class JoltCharacter;

        JPH::BodyID AddStatic(const JPH::ShapeRefC& shape,
                              const NS::Core::Vector3& position,
                              const NS::Core::Quaternion& rotation,
                              JPH::ObjectLayer layer,
                              bool sensor);

        JPH::BodyID SyncStatic(JPH::BodyID id,
                               const JPH::ShapeRefC& shape,
                               const NS::Core::Vector3& position,
                               const NS::Core::Quaternion& rotation,
                               JPH::ObjectLayer layer,
                               bool sensor);

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

        class ContactRecorder final : public JPH::ContactListener
        {
        public:
            void OnContactAdded(const JPH::Body& first,
                                const JPH::Body& second,
                                const JPH::ContactManifold& manifold,
                                JPH::ContactSettings& settings) override;

            void Clear() noexcept;
            [[nodiscard]] std::vector<BodyContact> Of(JPH::BodyID id) const;

        private:
            struct Record
            {
                JPH::BodyID owner;
                BodyContact contact;
            };

            std::vector<Record> m_records;
        };

        JPH::BodyID AddDynamic(const JPH::ShapeRefC& shape,
                               const NS::Core::Vector3& position,
                               const NS::Core::Quaternion& rotation,
                               const DynamicBodyDesc& desc);

        // m_tempAllocator より前に置く。構築が呼ぶ Jolt の確保関数は RegisterDefaultAllocator まで nullptr
        RuntimeInitialization m_runtimeInitialization;
        BroadPhaseLayerInterface m_broadPhaseLayerInterface;
        ObjectLayerPairFilter m_objectLayerPairFilter;
        ObjectVsBroadPhaseLayerFilter m_objectVsBroadPhaseLayerFilter;
        ContactRecorder m_contactRecorder;
        JPH::TempAllocatorImpl m_tempAllocator;
        JPH::JobSystemSingleThreaded m_jobSystem;
        JPH::PhysicsSystem m_physicsSystem;
    };
} // namespace NS::Physics
