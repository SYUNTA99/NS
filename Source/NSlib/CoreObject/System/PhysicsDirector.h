#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Phys/PhysicsScene.h"

namespace NS
{
    //! シーンの PhysicsScene を持ち、アクターが動いた後に broadphase の木を組み直す
    //! Collision は OnEndPlay で body を PhysicsScene から外すので、Scene
    //! は個体の一覧より先に宣言して後に破棄する
    class PhysicsDirector final : public NonCopyable
    {
    public:
        [[nodiscard]] Phys::PhysicsScene& Physics() noexcept { return m_physicsScene; }
        [[nodiscard]] const Phys::PhysicsScene& Physics() const noexcept { return m_physicsScene; }

        void OnTick();

    private:
        Phys::PhysicsScene m_physicsScene;
    };
} // namespace NS
