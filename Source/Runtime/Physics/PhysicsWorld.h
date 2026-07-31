#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/CollisionGrid.h"
#include "Runtime/Physics/SweptOBB.h"
#include "Runtime/Physics/SweptTriangle.h"

namespace NS::Physics
{
    /// capsule sweep の最初の接触結果。hit が false の時 toi は 1.0、normal は零ベクトル
    struct SweepHit
    {
        float toi = 1.0f;
        NS::Math::Vector3 normal{0.0f, 0.0f, 0.0f};
        bool hit = false;
    };

    /// @brief 静的衝突プリミティブ 5 channel と broadphase grid を持ち、capsule sweep と接地 probe を提供する衝突 world
    /// @details AABB / Triangle / OBB / Sphere / Capsule の 5 channel と AABB 専用 broadphase grid を持つ
    /// collision 再構築時に Clear -> Add* -> BuildBroadphase で満たし、SweepCapsule / ProbeGround で問い合わせる
    /// hazard 等の gameplay 判定は層が違うため含めない。NS::Physics は NS::Object に依存しない
    /// 依存: Math の AABB / OBB / Vector3、Capsule、Sphere、SweptOBB の sweep、SweptTriangle の Triangle、CollisionGrid
    class PhysicsWorld
    {
    public:
        // 構築。collision 再構築時に 1 度満たす

        /// 全 channel と grid を空にする
        void Clear() noexcept;

        /// AABB channel の領域を予約する。grid 配置物数が分かっている時の最適化
        void ReserveAabbs(std::size_t count);

        /// grid solid の軸並行 box を AABB channel へ追加する
        void AddAABB(const NS::Math::AABB& box);

        /// slope の world 空間三角形を Triangle channel へ追加する
        void AddTriangle(const Triangle& triangle);

        /// 回転 / scale 込みの自由配置物を OBB channel へ追加する
        void AddOBB(const NS::Math::OBB& obb);

        /// 球 collider を Sphere channel へ追加する
        void AddSphere(const NS::Math::Sphere& sphere);

        /// capsule collider を Capsule channel へ追加する
        void AddCapsule(const NS::Physics::Capsule& capsule);

        /// AABB channel から broadphase grid を構築する。Add 完了後に 1 度呼ぶ
        void BuildBroadphase() noexcept;

        // クエリ

        /// capsule が motion だけ動く間の最小 TOI の接触を全 channel から探して返す
        /// AABB は grid 候補、grid が無ければ総当たり
        /// 評価順は AABB -> Triangle -> OBB -> Sphere -> Capsule、同 TOI は先勝ち
        [[nodiscard]] SweepHit SweepCapsule(const NS::Physics::Capsule& cap,
                                            const NS::Math::Vector3& motion) const noexcept;

        /// bottomCenter から下方向へ reach 以内に AABB / OBB の床があれば true。接地判定の補助に使う
        [[nodiscard]] bool ProbeGround(const NS::Math::Vector3& bottomCenter, float reach) const noexcept;

        // アクセサ

        /// AABB channel への読み取り専用の参照。ledge grab の走査が使う
        [[nodiscard]] const std::vector<NS::Math::AABB>& Aabbs() const noexcept { return m_aabbs; }

        /// 全 channel が空かどうか。未 Build かプリミティブ無しなら空
        [[nodiscard]] bool IsEmpty() const noexcept;

    private:
        std::vector<NS::Math::AABB> m_aabbs;
        std::vector<Triangle> m_triangles;
        std::vector<NS::Math::OBB> m_obbs;
        std::vector<NS::Math::Sphere> m_spheres;
        std::vector<NS::Physics::Capsule> m_capsules;
        CollisionGrid m_grid;

        // sweep 候補の使い回しバッファ。const の query から確保なしで使うため mutable
        mutable std::vector<std::uint32_t> m_candidates;
    };
} // namespace NS::Physics
