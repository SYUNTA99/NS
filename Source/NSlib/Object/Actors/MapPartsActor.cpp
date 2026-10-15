#include "NSlib/Object/Actors/MapPartsActor.h"

#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/MeshCollision.h"
#include "NSlib/Object/SubObjects/Model.h"

namespace NS::Obj
{
    void MapPartsActor::OnInit()
    {
        // 色はテクスチャが無い時の見た目。床の既定の灰色
        Model* mesh = CreateSubObj(m_model);
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Vector3{0.70f, 0.70f, 0.75f});
        // 当たりは見た目のメッシュの三角形そのもの。メッシュを差し替えると当たりも付いて来る
        (void)CreateSubObj<Collision, MeshCollision>(m_collision);
    }

    NS_PLACEABLE(MapPartsActor, "地形の部品")
} // namespace NS::Obj
