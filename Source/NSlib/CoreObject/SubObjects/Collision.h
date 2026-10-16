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
    //! 所属シーンへ地形の body を登録する部品の基底。具体的な形状は派生が決める
    class Collision : public Object
    {
    public:
        explicit Collision(SceneActor* owner) noexcept : m_owner(owner) {}

        //! 世界座標の当たりを body 1 個として持ち主の Scene の PhysicsScene へ入れる。入れた body があれば置き直す
        //! 何も入れない形状もある。持ち主が Scene に居なければ何もしない
        void SyncToPhysics();

        //! 当たりの body の id。何も入れなかった形状では無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! 自分が入れた body を持ち主の Scene の PhysicsScene から外す。入れていなければ何もしない
        //! 持ち主が Scene に居なければ何もせず、id も持ったまま
        void RemoveFromPhysics();

        //! body を外す。所属シーンがなければエラーを出し、id だけを手放す
        void Unregister();

        NS_REFLECT_NONE(Collision, Object)

    protected:
        //! @brief 形の回転と中心のずれに、持ち主の root の世界行列を重ねた行列を返す
        //! @details 持ち主が居なければ、回転とずれの行列だけを返す
        [[nodiscard]] Matrix ShapeWorldMatrix(const Quaternion& localRotation,
                                                  const Vector3& centerOffset) const noexcept;

    private:
        // 無効な current は新規作成。所属と異なる PhysicsScene の id と混同しないよう、SyncToPhysics だけが呼ぶ
        [[nodiscard]] virtual JPH::BodyID SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current) = 0;

        JPH::BodyID m_bodyId;
        SceneActor* m_owner = nullptr; // 持ち主 (非所有)
    };
} // namespace NS
