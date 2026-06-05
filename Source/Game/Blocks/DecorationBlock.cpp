#include "Game/Blocks/DecorationBlock.h"
#include "Framework/Graphics/StaticMesh.h"

DecorationBlock::DecorationBlock(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
{
    m_mesh = AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
}
