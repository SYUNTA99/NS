#pragma once

#include "Runtime/Object/Component.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

namespace NS::Physics
{
    class PhysicsWorld;
    class JoltWorld;
} // namespace NS::Physics

namespace NS::Object
{
    //! @brief 当たり形状 Component の共通基底
    //! @details 自分の当たりをどの channel へ入れるかは形状自身が知っている
    //! world の組み直しはこの型だけを見て回るので、 形状を足しても組み直し側は変わらない
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Physics::PhysicsWorld, NS::Physics::JoltWorld, JPH::BodyID
    class ColliderComponent : public Component
    {
    public:
        // TODO: JoltWorld 版へ移り切ったら消す。 掃引と接地を問い合わせられるのは今も PhysicsWorld だけ
        //! world 座標の当たりを physics へ入れる。 何も入れない形状もある
        virtual void AddToPhysics(NS::Physics::PhysicsWorld& physics) const = 0;

        //! world 座標の当たりを body 1 個として physics へ入れる。 何も入れない形状もある
        virtual void AddToPhysics(NS::Physics::JoltWorld& physics) = 0;

        //! 入れた body の id。 何も入れなかった形状では無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! 覚えている body の id を捨てる
        //! RemoveAllBodies の後に通さないと、 次の AddToPhysics で ReplaceBody が既に無い body を消しに行って落ちる
        void ForgetBody() noexcept { m_bodyId = JPH::BodyID{}; }

        NS_REFLECT_NONE(ColliderComponent, Component)

    protected:
        // 前の body を外してから id を差し替える。 外さないと入れ直すたびに同じ形状の body が増える
        void ReplaceBody(NS::Physics::JoltWorld& physics, JPH::BodyID created) noexcept;

        JPH::BodyID m_bodyId;
    };
} // namespace NS::Object
