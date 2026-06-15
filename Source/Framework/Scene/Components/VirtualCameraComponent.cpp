#include "Framework/Scene/Components/VirtualCameraComponent.h"

namespace NS::Scene
{
    // out-of-line virtual dtor で vtable をこの TU に固定する (各 includer での重複生成を避ける)
    VirtualCameraComponent::~VirtualCameraComponent() noexcept = default;
} // namespace NS::Scene
