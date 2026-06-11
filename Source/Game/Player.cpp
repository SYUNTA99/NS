#include "Game/Player.h"
#include "Framework/Graphics/StaticMesh.h"

Player::Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material, NS::Platform::Input* input) noexcept
{
    // priority 順: m_input(0)→m_movement(200)。 m_input が m_movement を参照するため movement を先に生成する
    m_mesh = AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
    m_movement = AddComponent<NS::Scene::CharacterMovementComponent>();
    m_input = AddComponent<NS::Scene::PlayerInputComponent>(m_movement);
    m_input->SetInput(input);
}
