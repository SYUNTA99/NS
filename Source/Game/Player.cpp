#include "Game/Player.h"
#include "Framework/Graphics/StaticMesh.h"

Player::Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
{
    // m_input は OnStart で兄弟の m_movement を解決するため、参照目的の生成順の縛りは無い
    m_mesh = AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
    m_movement = AddComponent<NS::Scene::CharacterMovementComponent>();
    m_input = AddComponent<NS::Scene::PlayerInputComponent>();
    // 接地シャドウ。mesh / material / 衝突 world は LevelPlayScene が後から注入する
    m_shadow = AddComponent<NS::Scene::ShadowComponent>();
}
