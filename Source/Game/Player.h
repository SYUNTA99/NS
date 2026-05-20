#pragma once

#include "ns/scene/components/character_movement_component.h"
#include "ns/scene/components/mesh_component.h"
#include "ns/scene/components/player_input_component.h"
#include "ns/scene/game_object.h"

namespace ns::graphics
{
    class Material;
    class Mesh;
} // namespace ns::graphics

namespace ns::platform
{
    class Input;
} // namespace ns::platform

/// プレイヤーキャラクタの GameObject ( 固定スロット)。
/// MeshComponent + CharacterMovementComponent + PlayerInputComponent を named member
/// として保有する。Mesh / Material / Input は寿命を MainScene が保証。
class Player : public ns::scene::GameObject
{
public:
    Player(ns::graphics::Mesh* mesh, ns::graphics::Material* material, ns::platform::Input* input) noexcept;
    ~Player() override = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    [[nodiscard]] ns::scene::MeshComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] ns::scene::CharacterMovementComponent& Movement() noexcept { return m_movement; }
    [[nodiscard]] ns::scene::PlayerInputComponent& InputComp() noexcept { return m_input; }

private:
    ns::scene::MeshComponent m_mesh;
    ns::scene::CharacterMovementComponent m_movement;
    ns::scene::PlayerInputComponent m_input;
};
