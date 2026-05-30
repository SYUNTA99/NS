#include "Game/Blocks/DecorationBlock.h"

DecorationBlock::DecorationBlock(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material) noexcept
    : m_mesh(this, mesh, material)
{}
