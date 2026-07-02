#pragma once

#include "Framework/Scene/Components/CharacterMovementComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PlayerInputComponent.h"
#include "Framework/Scene/Components/ShadowComponent.h"
#include "Framework/Scene/GameObject.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

/// プレイヤーキャラクタ。 Mesh / Movement / Input の 3 Component を所有し参照をキャッシュする
class Player : public NS::Scene::GameObject
{
public:
    Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept;
    ~Player() override = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }
    [[nodiscard]] NS::Scene::CharacterMovementComponent& Movement() noexcept { return *m_movement; }
    [[nodiscard]] NS::Scene::PlayerInputComponent& InputComp() noexcept { return *m_input; }
    [[nodiscard]] NS::Scene::ShadowComponent& Shadow() noexcept { return *m_shadow; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
    NS::Scene::CharacterMovementComponent* m_movement = nullptr;
    NS::Scene::PlayerInputComponent* m_input = nullptr;
    NS::Scene::ShadowComponent* m_shadow = nullptr;
};
