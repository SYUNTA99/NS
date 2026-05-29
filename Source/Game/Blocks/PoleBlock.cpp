#include "Game/Blocks/PoleBlock.h"

PoleBlock::PoleBlock(NS::Graphics::Mesh* cylinderMesh,
                     NS::Graphics::Material* material,
                     float radius,
                     float height) noexcept
    : m_mesh(this, cylinderMesh, material), m_pole(this, radius, height)
{}
