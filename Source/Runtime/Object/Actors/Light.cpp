#include "Runtime/Object/Actors/Light.h"

#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    Light::Light() noexcept
    {
        AttachFixedComponent(m_light);
    }

    void Light::ForEachPart(const PartVisitor& visitor) const
    {
        Actor::ForEachPart(visitor);
        visitor("DirectionalLight", m_light);
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
