#include "Game/Blocks/WaterBlock.h"
#include "Framework/Graphics/StaticMesh.h"

WaterBlock::WaterBlock(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
    : m_mesh(this, mesh, material)
{}
