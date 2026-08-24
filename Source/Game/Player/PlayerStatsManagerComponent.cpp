#include "Game/Player/PlayerStatsManagerComponent.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Game::Player
{
    namespace
    {
        // 非有限値を捨てる。シーン JSON が外部データの唯一の入口で、重力や時定数へ入ると位置まで NaN が伝わる
        void AssignFinite(float& target, float value) noexcept
        {
            if (std::isfinite(value))
                target = value;
        }
    } // namespace

    void PlayerStatsManagerComponent::SetJumpImpulse(float value) noexcept
    {
        AssignFinite(CurrentMutable().jumpImpulse, value);
    }

    void PlayerStatsManagerComponent::SetGravityUp(float value) noexcept
    {
        AssignFinite(CurrentMutable().gravityUp, value);
    }

    void PlayerStatsManagerComponent::SetGravityDown(float value) noexcept
    {
        AssignFinite(CurrentMutable().gravityDown, value);
    }

    void PlayerStatsManagerComponent::SetApexHangVy(float value) noexcept
    {
        AssignFinite(CurrentMutable().apexHangVy, value);
    }

    void PlayerStatsManagerComponent::SetApexHangScale(float value) noexcept
    {
        AssignFinite(CurrentMutable().apexHangScale, value);
    }

    void PlayerStatsManagerComponent::SetJumpReleaseScale(float value) noexcept
    {
        AssignFinite(CurrentMutable().jumpReleaseScale, value);
    }

    void PlayerStatsManagerComponent::SetCoyoteTime(float value) noexcept
    {
        AssignFinite(CurrentMutable().coyoteTime, value);
    }

    void PlayerStatsManagerComponent::SetJumpBufferTime(float value) noexcept
    {
        AssignFinite(CurrentMutable().jumpBufferTime, value);
    }

    void PlayerStatsManagerComponent::SetWalkSpeed(float value) noexcept
    {
        AssignFinite(CurrentMutable().walkSpeed, value);
    }

    void PlayerStatsManagerComponent::SetAccelTau(float value) noexcept
    {
        AssignFinite(CurrentMutable().accelTau, value);
    }

    void PlayerStatsManagerComponent::SetDecelTau(float value) noexcept
    {
        AssignFinite(CurrentMutable().decelTau, value);
    }

    void PlayerStatsManagerComponent::SetStickDeadzone(float value) noexcept
    {
        AssignFinite(CurrentMutable().stickDeadzone, value);
    }

    void PlayerStatsManagerComponent::SetBodySlamSpeed(float value) noexcept
    {
        AssignFinite(CurrentMutable().bodySlamSpeed, value);
    }

    void PlayerStatsManagerComponent::SetBodySlamDistance(float value) noexcept
    {
        AssignFinite(CurrentMutable().bodySlamDistance, value);
    }

    void PlayerStatsManagerComponent::SetTapSlamSpeed(float value) noexcept
    {
        AssignFinite(CurrentMutable().tapSlamSpeed, value);
    }

    void PlayerStatsManagerComponent::SetTapSlamUpSpeed(float value) noexcept
    {
        AssignFinite(CurrentMutable().tapSlamUpSpeed, value);
    }

    void PlayerStatsManagerComponent::SetTapSlamDistance(float value) noexcept
    {
        AssignFinite(CurrentMutable().tapSlamDistance, value);
    }

    NS_CLASS(PlayerStatsManagerComponent)
} // namespace NS::Game::Player
