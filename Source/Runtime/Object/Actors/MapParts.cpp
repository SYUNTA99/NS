#include "Runtime/Object/Actors/MapParts.h"

#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    MapParts::MapParts() noexcept
    {
        // 色はテクスチャが無い時の見た目。床の既定の灰色
        MeshRenderer* mesh = AddComponent<MeshRenderer>();
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Core::Vector3{0.70f, 0.70f, 0.75f});
    }

    NS_PLACEABLE(MapParts, "地形の部品")
} // namespace NS::Obj
