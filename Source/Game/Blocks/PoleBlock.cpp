#include "Game/Blocks/PoleBlock.h"
#include "Framework/Graphics/StaticMesh.h"

PoleBlock::PoleBlock(NS::Graphics::StaticMesh* cylinderMesh,
                     NS::Graphics::Material* material,
                     float radius,
                     float height) noexcept
    : m_mesh(this, cylinderMesh, material), m_pole(this, radius, height)
{}
