#include "Game/Level/MapObj.h"

#include "Game/Level/Breakable.h"
#include "Game/Level/LaunchedBody.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    MapObj::MapObj() noexcept
    {
        NS::Obj::MeshRenderer* mesh = AddComponent<NS::Obj::MeshRenderer>();
        mesh->SetMeshRef("sphere");
        mesh->SetBaseColor(NS::Core::Vector3{0.72f, 0.70f, 0.66f});
        AddComponent<NS::Obj::SphereCollider>();
        // 置かれている間は動かない。押し飛ばされた時だけ LaunchedBody がダイナミックにする
        NS::Obj::RigidBody* body = AddComponent<NS::Obj::RigidBody>();
        body->SetKinematic(true);
        AddComponent<Breakable>();
        AddComponent<LaunchedBody>();
        AddComponent<NS::Obj::Shadow>();
    }

    NS_PLACEABLE(MapObj, "置物")
} // namespace NS::Game::Level
