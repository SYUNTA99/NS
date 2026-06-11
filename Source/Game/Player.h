#pragma once

#include "Framework/Scene/CharacterMovementComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshRendererComponent.h"
#include "Framework/Scene/PlayerInputComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

namespace NS::Platform
{
    class Input;
} // namespace NS::Platform

/// プレイヤーキャラクタ。 Mesh / Movement / Input の 3 Component を所有し参照をキャッシュする
class Player : public NS::Scene::GameObject
{
public:
    Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material, NS::Platform::Input* input) noexcept;
    ~Player() override = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }
    [[nodiscard]] NS::Scene::CharacterMovementComponent& Movement() noexcept { return *m_movement; }
    [[nodiscard]] NS::Scene::PlayerInputComponent& InputComp() noexcept { return *m_input; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
    NS::Scene::CharacterMovementComponent* m_movement = nullptr;
    NS::Scene::PlayerInputComponent* m_input = nullptr;
};
