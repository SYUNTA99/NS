#include "NSlib/CoreObject/Actors/MapPartsActor.h"

#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace NS
{
    void MapPartsActor::OnInit()
    {
        // 色はテクスチャが無い時の見た目。床の既定の灰色
        Model* mesh = CreateClassObj(m_model, this);
        mesh->SetMeshRef("cube");
        mesh->SetColorOverride(Color{0.70f, 0.70f, 0.75f});
        // 当たりは見た目のメッシュの三角形そのもの。メッシュを差し替えると当たりも付いて来る
        CreateClassObj<Collision, MeshCollision>(m_collision, this);
    }

    NS_PLACEABLE(MapPartsActor, "地形の部品")
} // namespace NS
