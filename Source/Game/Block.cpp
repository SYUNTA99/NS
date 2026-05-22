#include "Game/Block.h"

Block::Block(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material, const NS::Core::Vector3& halfExtents) noexcept
    : m_mesh(mesh, material), m_collider(halfExtents)
{
    RegisterComponent(&m_mesh);
    RegisterComponent(&m_collider);
}
