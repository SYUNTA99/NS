#include "Game/Player.h"

Player::Player() noexcept
{
    // コード既定の構成。値と追加分はファクトリが player object のデータから写す
    // input は OnStart で兄弟の movement を解決するため、参照目的の生成順の縛りは無い
    AddComponent<NS::Scene::MeshRendererComponent>(nullptr, nullptr);
    AddComponent<NS::Scene::CharacterMovementComponent>();
    AddComponent<NS::Scene::PlayerInputComponent>();
    // 接地シャドウ。mesh / material / 衝突 world は world の組み直しとファクトリが注入する
    AddComponent<NS::Scene::ShadowComponent>();
}
