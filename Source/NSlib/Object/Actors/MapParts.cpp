#include "NSlib/Object/Actors/MapParts.h"

#include "NSlib/Object/SubObjects/MeshCollision.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    void MapParts::Init()
    {
        // 色はテクスチャが無い時の見た目。床の既定の灰色
        Model* mesh = CreateSubObj<Model>(ModelSlot());
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Vector3{0.70f, 0.70f, 0.75f});
        // 当たりは見た目のメッシュの三角形そのもの。メッシュを差し替えると当たりも付いて来る
        (void)CreateSubObj<MeshCollision>(CollisionSlot());
    }

    NS_PLACEABLE(MapParts, "地形の部品")
} // namespace NS::Obj
