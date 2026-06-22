#include "Game/Blocks/HazardBlock.h"
#include "Framework/Graphics/StaticMesh.h"

HazardBlock::HazardBlock(NS::Graphics::StaticMesh* mesh,
                         NS::Graphics::Material* material,
                         const NS::Math::Vector3& halfExtents) noexcept
{
    m_mesh = AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
    m_collider = AddComponent<NS::Scene::BoxColliderComponent>(halfExtents);
    m_hazard = AddComponent<NS::Scene::HazardComponent>();
}
