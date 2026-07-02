#include "Game/Player.h"
#include "Framework/Graphics/StaticMesh.h"

Player::Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
{
    // コード既定の構成。追加分は PlayerTuning の読込が data から factory で加える
    // input は OnStart で兄弟の movement を解決するため、参照目的の生成順の縛りは無い
    AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
    AddComponent<NS::Scene::CharacterMovementComponent>();
    AddComponent<NS::Scene::PlayerInputComponent>();
    // 接地シャドウ。mesh / material / 衝突 world は LevelPlayScene が後から注入する
    AddComponent<NS::Scene::ShadowComponent>();
}
