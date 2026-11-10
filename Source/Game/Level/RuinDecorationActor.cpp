#include "Game/Level/RuinDecorationActor.h"

#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace GL
{
    namespace Level
    {
        void RuinDecorationActor::OnInit()
        {
            CreateClassObj(m_model);
        }

        NS_PLACEABLE(RuinDecorationActor, "Ruin decoration")
    }
}
