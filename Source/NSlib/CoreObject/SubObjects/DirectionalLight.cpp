#include "NSlib/CoreObject/SubObjects/DirectionalLight.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    NS_CLASS(DirectionalLight)

    Vector3 DirectionalLight::Direction() const noexcept
    {
        NS_ASSERT(Scene, m_owner != nullptr, "持ち主の無い DirectionalLight の向きを読んでいる");
        Vector3 direction = Vector3::TransformNormal(Vector3::UnitZ, m_owner->Root().WorldMatrix());
        direction.Normalize();
        return direction;
    }
} // namespace NS
