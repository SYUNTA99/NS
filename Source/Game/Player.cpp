#include "Game/Player.h"

Player::Player(ns::graphics::Mesh* mesh, ns::graphics::Material* material, ns::platform::Input* input) noexcept
    : m_mesh(mesh, material), m_movement(), m_input(&m_movement)
{
    RegisterComponent(&m_mesh);
    RegisterComponent(&m_movement);
    RegisterComponent(&m_input);
    m_input.SetInput(input);
}
