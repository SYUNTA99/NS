#include "Game/Block.h"

Block::Block(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material, const NS::Math::Vector3& halfExtents) noexcept
    : m_mesh(this, mesh, material), m_collider(this, halfExtents)
{}
