#include "Game/Player.h"
#include "Framework/Graphics/StaticMesh.h"

Player::Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material, NS::Platform::Input* input) noexcept
{
    // priority 昇順 OnUpdate:
    //   m_input    (Input,   0)  — jump 押下を SetJumpPressed で立てる
    //   m_mesh     (Physics, 200) — OnUpdate は何もしない (Draw のみ)
    //   m_movement (Physics, 200) — 同フレームで jump 消費 + 物理更新
    // 同 priority 内は登録順 (mesh を先に AddComponent) でタイブレーク
    // m_mesh は SET と CONSUME の間に挟まるが何もしないので jump feel に影響なし
    // m_input は m_movement を参照するため movement を先に生成する
    m_mesh = AddComponent<NS::Scene::MeshRendererComponent>(mesh, material);
    m_movement = AddComponent<NS::Scene::CharacterMovementComponent>();
    m_input = AddComponent<NS::Scene::PlayerInputComponent>(m_movement);
    m_input->SetInput(input);
}
