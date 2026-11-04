#include "Game/Level/WaterActor.h"

#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace GL
{
    namespace Level
    {
        void WaterActor::OnInit()
        {
            CreateClassObj(m_model);
            InitDraw(this, m_model.get(), &Root());
            m_model->SetDrawsAfterSky();
            m_model->SetNeverCulled();
        }

        NS_PLACEABLE(WaterActor, "水面")
    } // namespace Level
} // namespace GL
