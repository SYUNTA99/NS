#include "Game/Level/MomentumLockComponent.h"

#if !defined(NS_SHIPPING)

#include "Game/Level/MomentumComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>

namespace NS::Game::Level
{
    // -160 は MomentumComponent (-150) が最高速度を書く前に段を固定するため。後だと固定前の段の速度が 1 歩残る
    MomentumLockComponent::MomentumLockComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update - 160)
    {}

    void MomentumLockComponent::OnStart()
    {
        m_momentum = Owner()->FindComponent<MomentumComponent>();
    }

    void MomentumLockComponent::OnUpdate()
    {
        if (m_momentum == nullptr || m_lockLevel < 0)
            return;

        // 毎ステップ SetLevel し直す。積算と猶予が毎歩 0 へ戻り、固定中は昇格も降格も成立しない
        const int clamped = std::clamp(m_lockLevel, 0, 2);
        m_momentum->SetLevel(static_cast<MomentumLevel>(clamped));
    }

    NS_CLASS(MomentumLockComponent)
} // namespace NS::Game::Level

#endif
