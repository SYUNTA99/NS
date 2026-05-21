#include "Game/Player.h"

Player::Player(ns::graphics::Mesh* mesh, ns::graphics::Material* material, ns::platform::Input* input) noexcept
    : m_mesh(mesh, material), m_movement(), m_input(&m_movement)
{
    // input -> movement -> mesh の順に登録することで、
    // 同一 OnUpdate step 内で input.SetJumpPressed -> movement.OnUpdate(消費) が
    // 連続実行され、ジャンプ押下が次フレームに持ち越されない。逆順だと jump 押下が
    // 次フレームまで delay し、間に何かが m_jumpPressedThisFrame をリセットすると消失する。
    RegisterComponent(&m_input);
    RegisterComponent(&m_movement);
    RegisterComponent(&m_mesh);
    m_input.SetInput(input);
}
