#include "NSlib/Object/Actors/MapParts.h"

#include "NSlib/Object/Components/MeshCollision.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    MapParts::MapParts() noexcept
    {
        // 色はテクスチャが無い時の見た目。床の既定の灰色
        (void)CreatePart("Model");
        Model* mesh = ModelPart();
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Vector3{0.70f, 0.70f, 0.75f});
        // 当たりは見た目のメッシュの三角形そのもの。メッシュを差し替えると当たりも付いて来る
        SetCollisionPart(std::make_unique<MeshCollision>());
    }

    NS_PLACEABLE(MapParts, "地形の部品")
} // namespace NS::Obj
