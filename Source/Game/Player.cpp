#include "Game/Player.h"

namespace
{
    // テクスチャが揃うまでプレイヤーを block と見分ける個体色
    constexpr NS::Math::Vector3 kPlayerBaseColor{0.85f, 0.20f, 0.20f};
} // namespace

Player::Player() noexcept
{
    // コード既定の構成と見た目。値と追加分はファクトリが player object のデータから写す
    // input は OnStart で兄弟の movement を解決するため、参照目的の生成順の縛りは無い
    auto* mesh = AddComponent<NS::Scene::MeshRendererComponent>(nullptr, nullptr);
    // 参照はファクトリが cube mesh と共有 player 材質の実体へ解決する
    mesh->SetMeshRef("cube");
    mesh->SetMaterialRef("player");
    mesh->SetBaseColor(kPlayerBaseColor);
    AddComponent<NS::Scene::CharacterMovementComponent>();
    AddComponent<NS::Scene::PlayerInputComponent>();
    // 接地シャドウ。mesh / material / 衝突 world は world の組み直しとファクトリが注入する
    AddComponent<NS::Scene::ShadowComponent>();
}
