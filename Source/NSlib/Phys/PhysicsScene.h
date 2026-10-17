#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Phys/Capsule.h"
#include "NSlib/Phys/MeshShape.h"
#include "NSlib/Phys/ShapePart.h"
#include "NSlib/Phys/Triangle.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <span>
#include <vector>

namespace NS
{
    namespace Phys
    {
        //! body の種別を表す ObjectLayer。どの組み合わせが当たるかは ObjLayerPairFilter が持つ
        namespace ObjectLayers
        {
            inline constexpr JPH::ObjectLayer Terrain = 0;
            inline constexpr JPH::ObjectLayer Rock = 1;
            inline constexpr uint Count = 2;
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
            inline constexpr JPH::BroadPhaseLayer Rock = FromObjectLayer(ObjectLayers::Rock);
            inline constexpr uint Count = ObjectLayers::Count;
        } // namespace BroadPhaseLayers

        //! @brief 重力の既定の Y 成分 (m/s^2)
        //! @details 自機の上昇重力と同じ値。下降の -35
        //! は頂点から早く落として操作を返すための値で、操作の無い物には掛けない
        inline constexpr float k_DefaultGravityY = -25.0f;

        //! 重力の既定値。真下へ k_DefaultGravityY
        [[nodiscard]] inline Vector3 DefaultGravity() noexcept
        {
            return Vector3{0.0f, k_DefaultGravityY, 0.0f};
        }

        //! @brief 直近の Update で記録した接触 1 件
        struct BodyContact
        {
            JPH::BodyID other;  // ぶつかった相手の body
            Vector3 normal; // 接触面の法線。持ち主を相手から離す向き
        };

        //! @brief 動的 body を作る時の設定
        struct DynamicBodyDesc
        {
            JPH::ObjectLayer layer = ObjectLayers::Rock; // 当たる相手を決める種別
            float mass = 1.0f;                           // 質量。0 以下なら 1 として作る
            float restitution = 0.0f;                    // 跳ね返り
            float friction = 0.2f;                       // 摩擦
        };

        //! @brief 動く body の動き方・質量・材質
        //! @details PhysicsScene が受け取る時に直す値がある
        //! 非有限値は既定値にし、負の摩擦・跳ね返り・減衰は 0 にし、0 以下の質量は 1 にする
        struct BodyMotion
        {
            bool kinematic = false;           // 真なら力を受けず、MoveKinematic で運ばれて相手を押す
            float mass = 1.0f;                // 質量 (kg)
            float friction = 0.2f;            // 摩擦
            float restitution = 0.0f;         // 跳ね返り
            float linearDamping = 0.05f;      // 移動の減衰。速度が毎秒この割合で落ちる
            float angularDamping = 0.05f;     // 回転の減衰
            float gravityFactor = 1.0f;       // 世界の重力に掛ける倍率。0 なら重力を受けない
            bool continuousCollision = false; // 真なら 1 歩で動いた道を掃引して当て、速い物のすり抜けを防ぐ
            //! 動ける軸。世界の軸で数える。全部塞ぐと力で動かせないので、キネマティックとして作る
            JPH::EAllowedDOFs allowedDOFs = JPH::EAllowedDOFs::All;

            [[nodiscard]] bool operator==(const BodyMotion& other) const = default;
        };

        //! Jolt の物理世界。初回の構築かメッシュ形状の生成時に共通の型登録を行う
        //! Add 系で形状を作れなければ無効な BodyID
        class PhysicsScene : public NonCopyable
        {
        public:
            PhysicsScene();
            ~PhysicsScene();

            //! 入っている body の数
            [[nodiscard]] uint BodyCount() const noexcept;

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

            //! OBB の中心と 3 軸をそのまま動的な box body にする
            JPH::BodyID AddDynamicBox(const OBB& box, const DynamicBodyDesc& desc);
            //! 中心と半径をそのまま動的な球 body にする
            JPH::BodyID AddDynamicSphere(const Sphere& sphere, const DynamicBodyDesc& desc);

            //! 世界座標の形状群を動く body にまとめ、位置と回転を更新する。速度は保つ
            //! 無効な id または静的 body は新規作成し、元の body は削除しない
            //! 動的に使えない形状は除外し、有効な形状がなければ無効な BodyID
            JPH::BodyID SyncMovingBody(JPH::BodyID id,
                                       std::span<const ShapePart> parts,
                                       const Vector3& position,
                                       const Quaternion& rotation,
                                       const BodyMotion& motion,
                                       JPH::ObjectLayer layer = ObjectLayers::Rock);

            //! 動く body の動き方・質量・材質を motion へ書き換えて起こす。無効な BodyID と静的な body は何もしない
            void SetBodyMotion(JPH::BodyID id, const BodyMotion& motion);

            //! @brief キネマティックの body を、次の Update の deltaTime 秒で position・rotation へ着くよう動かす
            //! @details 瞬間移動ではなく速度を持たせて運ぶので、途中で触れた相手を押す
            //! 無効な BodyID と、deltaTime が正でない時は何もしない
            void MoveKinematic(JPH::BodyID id,
                               const Vector3& position,
                               const Quaternion& rotation,
                               float deltaTime);

            //! body を position・rotation へ瞬間移動させて起こす。速度はそのまま。無効な BodyID は何もしない
            void TeleportBody(JPH::BodyID id, const Vector3& position, const Quaternion& rotation);

            //! 次の Update の間、重心へ力 (N) を掛け続ける。無効な BodyID は何もしない
            void AddBodyForce(JPH::BodyID id, const Vector3& force);
            //! 重心へ力積 (N·s) を与え、速度を即座に変える。無効な BodyID は何もしない
            void AddBodyImpulse(JPH::BodyID id, const Vector3& impulse);
            //! 次の Update の間、トルク (N·m) を掛け続ける。無効な BodyID は何もしない
            void AddBodyTorque(JPH::BodyID id, const Vector3& torque);
            //! 角力積 (N·m·s) を与え、角速度を即座に変える。無効な BodyID は何もしない
            void AddBodyAngularImpulse(JPH::BodyID id, const Vector3& angularImpulse);
            //! 眠っている body を起こす。無効な BodyID は何もしない
            void WakeBody(JPH::BodyID id);

            //! @details 置き換えるのは動く body に掛かる重力だけ。自機は重力を自分で持つので変わらない
            //! 非有限の成分を含む値は受け取らない
            void SetGravity(const Vector3& gravity);
            //! 世界の重力 (m/s^2)
            [[nodiscard]] Vector3 Gravity() const;

            //! body の角速度を置く。無効な BodyID は何もしない
            void SetBodyAngularVelocity(JPH::BodyID id, const Vector3& angularVelocity);
            //! body の角速度
            [[nodiscard]] Vector3 BodyAngularVelocity(JPH::BodyID id) const;
            //! body が起きている場合 true、それ以外の場合は false。無効な BodyID は false
            [[nodiscard]] bool IsBodyAwake(JPH::BodyID id) const;
            //! 直近の Update で記録した id の接触。前の Update の分は残らない。無効な BodyID は空
            [[nodiscard]] std::vector<BodyContact> ContactsOf(JPH::BodyID id) const;

            //! broadphase の木を組み直す。Add 完了後に 1 度呼ぶ
            void OptimizeBroadPhase();

            //! この PhysicsScene を deltaTime 秒ぶん進める。衝突の分割は 1 で、渡した時間を刻まない
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

            //! @brief capsule に重なっている body の id を集めて返す
            //! @details 形の実物どうしで見るので、回転した box は外接箱ではなく本当の形で判定する
            //! layer は問わない。同じ body は 1 度だけ返る
            [[nodiscard]] std::vector<JPH::BodyID> OverlapCapsule(const Capsule& capsule) const;

            //! @brief region に重なる body の世界座標の境界箱を集めて返す
            //! @details 重なりを見るのも返すのも軸並行の境界箱で、shape の形は見ない
            //! 回転した box や mesh では形より大きい箱が返る
            [[nodiscard]] std::vector<AABB> OverlapBox(const AABB& region) const;

            //! body の線速度を置く。無効な BodyID は何もしない
            void SetBodyVelocity(JPH::BodyID id, const Vector3& velocity);

            //! body の線速度
            [[nodiscard]] Vector3 BodyVelocity(JPH::BodyID id) const;

            //! body をこの PhysicsScene から外して壊す。無効な BodyID は何もしない
            void RemoveBody(JPH::BodyID id);

            //! body の世界座標の位置
            [[nodiscard]] Vector3 BodyPosition(JPH::BodyID id) const;
            //! body の世界座標の回転
            [[nodiscard]] Quaternion BodyRotation(JPH::BodyID id) const;

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
                                   const Vector3& position,
                                   const Quaternion& rotation,
                                   const DynamicBodyDesc& desc);

            // m_tempAllocator より前に置く。構築が呼ぶ Jolt の確保関数は RegisterDefaultAllocator まで nullptr
            RuntimeInit m_runtimeInitialization;
            BPLayerInterface m_broadPhaseLayerInterface;
            ObjLayerPairFilter m_objectLayerPairFilter;
            ObjVsBPLayerFilter m_objectVsBroadPhaseLayerFilter;
            ContactRecorder m_contactRecorder;
            JPH::TempAllocatorImpl m_tempAllocator;
            JPH::JobSystemSingleThreaded m_jobSystem;
            JPH::PhysicsSystem m_physicsSystem;
        };
    } // namespace Phys
} // namespace NS
