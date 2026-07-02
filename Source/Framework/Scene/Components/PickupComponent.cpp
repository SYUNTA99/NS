#include "Framework/Scene/Components/PickupComponent.h"

#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"

namespace NS::Scene
{
    PickupComponent::PickupComponent() noexcept = default;

    NS_REGISTER_COMPONENT(PickupComponent)
} // namespace NS::Scene
