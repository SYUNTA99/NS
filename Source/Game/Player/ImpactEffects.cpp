#include "Game/Player/ImpactEffects.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Player
{
    ImpactEffects::ImpactEffects() noexcept : NS::Obj::Component(NS::Obj::TickPriority::Update + 60) {}

    void ImpactEffects::OnUpdate()
    {
        m_layers.BeginStep(EffectsOf(*this));
    }

    NS_CLASS(ImpactEffects)
} // namespace NS::Game::Player
