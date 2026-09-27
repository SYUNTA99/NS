#include "Game/Player/ChargeEffects.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Player
{
    ChargeEffects::ChargeEffects() noexcept : NS::Obj::Component(NS::Obj::TickPriority::Update + 60) {}

    void ChargeEffects::OnUpdate()
    {
        m_layers.BeginStep(EffectsOf(*this));
    }

    NS_CLASS(ChargeEffects)
} // namespace NS::Game::Player
