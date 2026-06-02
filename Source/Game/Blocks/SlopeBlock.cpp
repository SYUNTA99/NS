#include "Game/Blocks/SlopeBlock.h"

SlopeBlock::SlopeBlock(NS::Graphics::Mesh* wedgeMesh,
                       NS::Graphics::Material* material,
                       float angleDegrees,
                       const NS::Math::Vector3& halfExtents) noexcept
    : m_mesh(this, wedgeMesh, material), m_collider(this, angleDegrees, halfExtents)
{}
