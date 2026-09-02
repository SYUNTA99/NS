#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Physics
{
    class PhysicsWorld;
} // namespace NS::Physics

namespace NS::Object
{
    //! @brief 当たり形状 Component の共通基底
    //! @details 自分の当たりをどの channel へ入れるかは形状自身が知っている
    //! world の組み直しはこの型だけを見て回るので、 形状を足しても組み直し側は変わらない
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Physics::PhysicsWorld
    class ColliderComponent : public Component
    {
    public:
        //! world 座標の当たりを physics へ入れる。 何も入れない形状もある
        virtual void AddToPhysics(NS::Physics::PhysicsWorld& physics) const = 0;

        NS_REFLECT_NONE(ColliderComponent, Component)
    };
} // namespace NS::Object
