#pragma once

#include "Runtime/Object/Component.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

namespace NS::Physics
{
    class PhysicsWorld;
}

namespace NS::Object
{
    //! @brief 当たり形状 Component の共通基底
    //! @details どの形の body として physics へ入れるかは派生が決める
    //! world の同期はこの型だけを見て回るので、形状を足しても同期側は変わらない
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Physics::PhysicsWorld, JPH::BodyID
    class ColliderComponent : public Component
    {
    public:
        //! world 座標の当たりを body 1 個として physics へ入れる。 何も入れない形状もある
        virtual void SyncToPhysics(NS::Physics::PhysicsWorld& physics) = 0;

        //! 入れた body の id。 何も入れなかった形状では無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! 自分が入れた body を world から外す。 入れていなければ何もしない
        void RemoveFromPhysics() noexcept;

        //! 配置物ごと消える前に自分の body を外す
        void OnEndPlay() override;

        NS_REFLECT_NONE(ColliderComponent, Component)

    protected:
        // 同じ world の同期では id を維持し、world または body が変わった時だけ以前の body を外す
        void TrackBody(NS::Physics::PhysicsWorld& physics, JPH::BodyID body) noexcept;
        [[nodiscard]] JPH::BodyID BodyIn(const NS::Physics::PhysicsWorld& physics) const noexcept;

        JPH::BodyID m_bodyId;
        NS::Physics::PhysicsWorld* m_physics = nullptr;
    };
} // namespace NS::Object
