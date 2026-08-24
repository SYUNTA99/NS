#include "Game/Player/PlayerComponent.h"

#include "Game/Entity/EntityStateManagerComponent.h"
#include "Game/Player/PlayerStatsManagerComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Game::Player
{
    const PlayerStats& PlayerComponent::Stats() const noexcept
    {
        if (m_statsManager != nullptr)
            return m_statsManager->Current();

        // 組が無いのはシーンを組まない検証台だけ。既定値は組の既定と同じなので手触りは変わらない
        static const PlayerStats k_Default{};
        return k_Default;
    }

    void PlayerComponent::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void PlayerComponent::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = NS::Core::Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = NS::Core::Clamp(localForward, -1.0f, 1.0f);
    }

    void PlayerComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void PlayerComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    float PlayerComponent::CoyoteTime() const noexcept
    {
        return Stats().coyoteTime;
    }

    void PlayerComponent::SetMaxSpeed(float speed) noexcept
    {
        // 非有限値は入口で捨てる。CapsuleMover は速度を検査しないので位置まで NaN が伝わる
        if (!std::isfinite(speed))
            return;

        if (speed < 0.0f)
            m_maxSpeed = 0.0f;
        else
            m_maxSpeed = speed;
    }

    void PlayerComponent::ResetState() noexcept
    {
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_desiredDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredSpeedScale = 0.0f;
        m_climbRight = 0.0f;
        m_climbForward = 0.0f;
        m_jumpHeld = false;
        m_prevJumpHeld = false;
        m_jumpPressedThisFrame = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        SetGrounded(false);
        m_lastGroundedPosition = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_coyoteJumpMarkers.clear();

        if (m_stateManager != nullptr)
            m_stateManager->ResetToFirst();
    }

    void PlayerComponent::OnStart()
    {
        NS::Game::Entity::EntityComponent::OnStart();

        if (Owner() == nullptr)
            return;

        m_statsManager = Owner()->FindComponent<PlayerStatsManagerComponent>();
        m_stateManager = Owner()->FindComponent<NS::Game::Entity::EntityStateManagerComponent>();
    }

    void PlayerComponent::HandleStates(float)
    {
        // 1 歩限りの入力の消費は、どの状態でも通るここで行う
        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
    }

    void PlayerComponent::OnStepSkipped()
    {
        m_jumpPressedThisFrame = false;
        m_prevJumpHeld = m_jumpHeld;
    }

    NS_CLASS(PlayerComponent)
} // namespace NS::Game::Player
