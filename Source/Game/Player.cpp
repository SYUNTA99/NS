#include "Game/Player.h"
#include "Framework/Graphics/StaticMesh.h"

Player::Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material, NS::Platform::Input* input) noexcept
{
    // priority 順は m_input が 0 で m_movement が 200。 m_input が m_movement を参照するため movement を先に生成する
    m_mesh = AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
    m_movement = AddComponent<NS::Scene::CharacterMovementComponent>();
    m_input = AddComponent<NS::Scene::PlayerInputComponent>(m_movement);
    m_input->SetInput(input);
    // 接地シャドウ。mesh / material / 衝突 world は LevelPlayScene が後から注入する
    m_shadow = AddComponent<NS::Scene::ShadowComponent>();
}
