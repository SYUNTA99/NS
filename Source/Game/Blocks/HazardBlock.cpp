#include "Game/Blocks/HazardBlock.h"

HazardBlock::HazardBlock(NS::Graphics::Mesh* mesh,
                         NS::Graphics::Material* material,
                         const NS::Math::Vector3& halfExtents) noexcept
    : m_mesh(this, mesh, material), m_collider(this, halfExtents), m_hazard(this)
{}
