#include "Framework/Scene/Components/HazardComponent.h"

#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"

namespace NS::Scene
{
    HazardComponent::HazardComponent() noexcept {}

    NS_REGISTER_COMPONENT(HazardComponent)
} // namespace NS::Scene
