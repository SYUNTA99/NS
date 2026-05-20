#include "Game/Block.h"

Block::Block(ns::graphics::Mesh* mesh, ns::graphics::Material* material, const ns::core::Vector3& halfExtents) noexcept
    : m_mesh(mesh, material), m_collider(halfExtents)
{
    RegisterComponent(&m_mesh);
    RegisterComponent(&m_collider);
}
