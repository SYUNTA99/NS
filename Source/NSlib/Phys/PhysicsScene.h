#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Phys/Capsule.h"
#include "NSlib/Phys/MeshShape.h"
#include "NSlib/Phys/Triangle.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <span>
#include <vector>

namespace NS
{
    namespace Phys
    {
        //! body の種別を表す ObjectLayer。body はどれも動かないので、body どうしは当たらない
        namespace ObjectLayers
        {
            inline constexpr JPH::ObjectLayer Terrain = 0;
            inline constexpr uint Count = 1;
        } // namespace ObjectLayers

        namespace BroadPhaseLayers
        {
            //! @brief ObjectLayer と同じ番号の BroadPhaseLayer を作る
            //! @details BroadPhaseLayerInterface は ObjectLayer をそのまま番号へキャストして割り当てる
            //! 定数側にも番号を書くと、ObjectLayers を並べ替えた時に片方だけ古い番号が残る
            constexpr JPH::BroadPhaseLayer FromObjectLayer(JPH::ObjectLayer layer) noexcept
            {
                return JPH::BroadPhaseLayer{static_cast<JPH::BroadPhaseLayer::Type>(layer)};
            }

            inline constexpr JPH::BroadPhaseLayer Terrain = FromObjectLayer(ObjectLayers::Terrain);
            inline constexpr uint Count = ObjectLayers::Count;
        } // namespace BroadPhaseLayers

        //! 重力の向き。真下に決めて、シーンでは変えない
        inline constexpr Vector3 k_GravityDirection{0.0f, -1.0f, 0.0f};

        //! 速度を重力の向きへ strength × dt だけ加速する。strength は m/s²、dt は秒
        //! 非有限値で速度が壊れると戻せないので、非有限値か dt が 0 以下なら変更しない
        inline void AddGravity(Vector3& velocity, float strength, float dt) noexcept
        {
            if (!std::isfinite(strength) || !std::isfinite(dt) || !(dt > 0.0f))
            {
                return;
            }
            velocity += k_GravityDirection * (strength * dt);
        }

        //! Jolt の物理世界。初回の構築かメッシュ形状の生成時に共通の型登録を行う
        //! Add 系で形状を作れなければ無効な BodyID
        class PhysicsScene : public NonCopyable
        {
        public:
            PhysicsScene();
            ~PhysicsScene();

            //! 作った body の数。DetachBody で外しただけの body も数える
            [[nodiscard]] uint BodyCount() const noexcept;
            //! 当たりの問いと接触に出る body の数。DetachBody で外した body を除く
            [[nodiscard]] uint AttachedBodyCount() const;

            //! OBB の中心と 3 軸をそのまま box body にする
            JPH::BodyID AddBox(const OBB& box, JPH::ObjectLayer layer);
            //! id の body を box の形と姿勢・layer へ書き換えて id を返す。id が無効なら新しく作る
            JPH::BodyID SyncBox(JPH::BodyID id, const OBB& box, JPH::ObjectLayer layer);
            //! 中心と半径をそのまま球 body にする
            JPH::BodyID AddSphere(const Sphere& sphere, JPH::ObjectLayer layer);
            //! id の body を sphere の形と位置・layer へ書き換えて id を返す。id が無効なら新しく作る
            JPH::BodyID SyncSphere(JPH::BodyID id, const Sphere& sphere, JPH::ObjectLayer layer);
            //! capsule body を capsule.axis の向きで入れる。軸が零ベクトルなら Y 軸
            JPH::BodyID AddCapsule(const Capsule& capsule, JPH::ObjectLayer layer);
            //! id の body を capsule の形と姿勢・layer へ書き換えて id を返す。id が無効なら新しく作る
            JPH::BodyID SyncCapsule(JPH::BodyID id, const Capsule& capsule, JPH::ObjectLayer layer);
            //! 三角形群を 1 つの静的 body にする。空なら作らない
            JPH::BodyID AddMesh(std::span<const Triangle> triangles, JPH::ObjectLayer layer);
            //! id の body を三角形群の形と layer へ書き換えて id を返す。id が無効なら新しく作る
            //! 空か、形を作れなければ無効な BodyID を返し、id の body は外さない
            JPH::BodyID SyncMesh(JPH::BodyID id, std::span<const Triangle> triangles, JPH::ObjectLayer layer);
            //! @brief id の body を collision の形で、位置・回転・拡縮へ置いて id を返す。id が無効なら新しく作る
            //! @details 形は作り直さずに共有する。拡縮が 1 でなければ、共有した形を拡縮つきの形で包む
            //! 位置・回転・拡縮で表せない歪みは受け取れない。歪みのある配置は SyncMesh に世界座標の三角形を渡す
            //! 形がないか、拡縮の 3 軸とも零に近ければ無効な BodyID を返す。既存の物体は外さない
            JPH::BodyID SyncMeshShape(JPH::BodyID id,
                                      const MeshShape& collision,
                                      const Vector3& position,
                                      const Quaternion& rotation,
                                      const Vector3& scale,
                                      JPH::ObjectLayer layer);

            //! broadphase の木を組み直す。Add 完了後に 1 度呼ぶ
            void OptimizeBroadPhase();

            //! @brief broadphase の木を組み直す。固定更新ごとに 1 度呼ぶ
            //! @details body は動かないので進める物は無い。Sync 系で置き直した body は置いた時から問い合わせに出るが、
            //! 木の境界は広がるだけなので、ここで締める。進める物が無いので、deltaTime の値で結果は変わらない
            void Update(float deltaTime);

            //! 光線上の最も近い命中までの世界距離を返す。direction の長さは問わず、全 body を調べる
            //! maxDistance か direction の長さが正でなければ false。未命中では outDistance を変更しない
            [[nodiscard]] bool Raycast(const Vector3& origin,
                                       const Vector3& direction,
                                       float maxDistance,
                                       float& outDistance) const;

            //! ignoredBody を除き、最も近い命中の世界距離と面の世界法線を返す
            //! 非有限値・正でない距離や方向長・未命中では、出力を変えず false
            [[nodiscard]] bool Raycast(const Vector3& origin,
                                       const Vector3& direction,
                                       float maxDistance,
                                       float& outDistance,
                                       Vector3& outNormal,
                                       JPH::BodyID ignoredBody) const;

            //! @brief region に重なる body の世界座標の境界箱を集めて返す
            //! @details 重なりを見るのも返すのも軸並行の境界箱で、shape の形は見ない
            //! 回転した box や mesh では形より大きい箱が返る
            [[nodiscard]] std::vector<AABB> OverlapBox(const AABB& region) const;

            //! body をこの PhysicsScene から外して壊す。DetachBody で外してある body も壊す。無効な BodyID は何もしない
            void RemoveBody(JPH::BodyID id);
            //! body を壊さずに外す。外した body は当たりの問いにも接触にも出ない。無効な BodyID と外してある body は何もしない
            void DetachBody(JPH::BodyID id);
            //! DetachBody で外した body を戻す。無効な BodyID と入っている body は何もしない
            void AttachBody(JPH::BodyID id);

        private:
            friend class JoltCharacter;

            JPH::BodyID SyncStatic(JPH::BodyID id,
                                   const JPH::ShapeRefC& shape,
                                   const Vector3& position,
                                   const Quaternion& rotation,
                                   JPH::ObjectLayer layer);

            class RuntimeInit
            {
            public:
                RuntimeInit();
            };

            class BPLayerInterface final : public JPH::BroadPhaseLayerInterface
            {
            public:
                [[nodiscard]] uint GetNumBroadPhaseLayers() const override;
                [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
            };

            class ObjLayerPairFilter final : public JPH::ObjectLayerPairFilter
            {
            public:
                [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override;
            };

            class ObjVsBPLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
            {
            public:
                [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer object,
                                                 JPH::BroadPhaseLayer broadPhase) const override;
            };

            // m_tempAllocator より前に置く。構築が呼ぶ Jolt の確保関数は RegisterDefaultAllocator まで nullptr
            RuntimeInit m_runtimeInitialization;
            BPLayerInterface m_broadPhaseLayerInterface;
            ObjLayerPairFilter m_objectLayerPairFilter;
            ObjVsBPLayerFilter m_objectVsBroadPhaseLayerFilter;
            JPH::TempAllocatorImpl m_tempAllocator;
            JPH::JobSystemSingleThreaded m_jobSystem;
            JPH::PhysicsSystem m_physicsSystem;
        };
    } // namespace Phys
} // namespace NS
