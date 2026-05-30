#include "Game/Blocks/WaterBlock.h"

WaterBlock::WaterBlock(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material) noexcept
    : m_mesh(this, mesh, material)
{}
