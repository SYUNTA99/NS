#include "Game/Blocks/SlopeBlock.h"
#include "Framework/Graphics/StaticMesh.h"

SlopeBlock::SlopeBlock(NS::Graphics::StaticMesh* wedgeMesh,
                       NS::Graphics::Material* material,
                       float angleDegrees,
                       const NS::Math::Vector3& halfExtents) noexcept
    : m_mesh(this, wedgeMesh, material), m_collider(this, angleDegrees, halfExtents)
{}
