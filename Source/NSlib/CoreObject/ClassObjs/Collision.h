#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

namespace NS
{
    namespace Phys
    {
        class PhysicsScene;
    }
} // namespace NS

namespace NS
{
    class IGetCollision;

    //! 地形の body を物理シーンへ入れる部品の基底。具体的な形状は派生が決める
    //! 持ち主を覚えない。持ち主の root の世界行列と物理シーン (窓口 IGetCollision) は呼ぶたびに受ける
    class Collision : public Object
    {
    public:
        //! 世界座標の当たりを body 1 個として user の PhysicsScene へ入れる。入れた body があれば置き直す
        //! 前に入れた時から world も形も変わっていなければ何もしない。何も入れない形状もある。
        //!          user がシーンに居なければ何もしない。事前条件: 持ち主が出ている (消えている間は呼ばない)
        //! @param[in] world 持ち主の root の世界行列
        void SyncToPhysics(const Matrix& world, const IGetCollision* user);

        //! 当たりの body の id。何も入れなかった形状では無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! 入れた body を壊さずに user の PhysicsScene から外す。id は持ったままで、ReturnToPhysics で戻す
        //! body が無いか、user がシーンに居なければ何もしない
        void RemoveFromPhysics(const IGetCollision* user);

        //! RemoveFromPhysics で外した body を戻し、world へ置き直す。body がまだ無ければ作る
        //! @param[in] world 持ち主の root の世界行列
        void ReturnToPhysics(const Matrix& world, const IGetCollision* user);

        //! body を壊す。user がシーンに居なければエラーを出し、id だけを手放す。body が無ければ何もしない
        void Unregister(const IGetCollision* user);

        NS_REFLECT_NONE(Collision, Object)

    protected:
        //! 形の回転と中心のずれに、持ち主の root の世界行列 world を重ねた行列を返す
        [[nodiscard]] static Matrix ShapeWorldMatrix(const Matrix& world,
                                                     const Quaternion& localRotation,
                                                     const Vector3& centerOffset) noexcept;

        //! 形を変える関数 (欄の setter と形の差し替え) が呼ぶ。次の SyncToPhysics が姿の比べを飛ばして入れ直す
        void MarkShapeChanged() noexcept { m_shapeChanged = true; }

    private:
        // 無効な current は新規作成。所属と異なる PhysicsScene の id と混同しないよう、SyncToPhysics だけが呼ぶ
        [[nodiscard]] virtual JPH::BodyID SyncBody(Phys::PhysicsScene& physics,
                                                   JPH::BodyID current,
                                                   const Matrix& world) = 0;

        JPH::BodyID m_bodyId;
        // 最後に入れた時の持ち主の世界行列。入れていない間は m_synced が偽
        Matrix m_syncedWorld;
        bool m_synced = false;
        bool m_shapeChanged = false;
    };
} // namespace NS
