#include "Game/Player.h"

Player::Player(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material, NS::Platform::Input* input) noexcept
    : m_mesh(this, mesh, material), m_movement(this), m_input(this, &m_movement)
{
    // priority 昇順 OnUpdate (data member 化によりコンストラクタ内で正しく sort される):
    //   m_input    (Input,   0)  — jump 押下を SetJumpPressed で立てる
    //   m_mesh     (Physics, 200) — OnUpdate は no-op (Draw のみ)
    //   m_movement (Physics, 200) — 同フレームで jump 消費 + 物理更新
    // 同 priority 内は declaration 順 (Player.h で m_mesh が先) でタイブレーク
    // m_mesh は SET と CONSUME の間に挟まるが no-op なので jump feel に影響なし
    m_input.SetInput(input);
}
