#include "NSlib/CoreObject/Actors/MapPartsActor.h"

#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace NS
{
    void MapPartsActor::OnInit()
    {
        CreateClassObj(m_model);
        InitDraw(this, m_model.get(), &Root());
        // 当たりは見た目のメッシュの三角形そのもの。メッシュを差し替えると当たりも付いて来る
        CreateClassObj<Collision, MeshCollision>(m_collision);
    }

    NS_PLACEABLE(MapPartsActor, "地形の部品")
} // namespace NS
