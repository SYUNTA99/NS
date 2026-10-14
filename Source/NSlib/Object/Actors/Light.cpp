#include "NSlib/Object/Actors/Light.h"

#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/DirectionalLight.h"

namespace NS::Obj
{
    void Light::VisitExtraSubObjs(const SubObjVisitor& visit) const
    {
        VisitSubObjMembers(visit, m_light);
    }

    void Light::OnInit()
    {
        (void)CreateSubObj(m_light, "DirectionalLight");
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
