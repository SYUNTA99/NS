#include "Game/Player/PlayerComponent.h"

#include "Game/Player.h"
#include "Game/Player/HorizontalTurn.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace NS::Game::Player
{
    const PlayerParams& PlayerComponent::Tuning() const noexcept
    {
        if (m_params != nullptr)
        {
            return *m_params;
        }
        static const PlayerParams defaults;
        return defaults;
    }

    const NS::Obj::PlayerInput& PlayerComponent::Input() const noexcept
    {
        if (m_input != nullptr)
        {
            return *m_input;
        }
        static const NS::Obj::PlayerInput neutral;
        return neutral;
    }

    void PlayerComponent::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        if (m_input != nullptr)
        {
            m_input->SetDesiredMove(worldDir, speedScale01);
        }
    }

    void PlayerComponent::SetClimbMove(float localRight, float localForward) noexcept
    {
        if (m_input != nullptr)
        {
            m_input->SetClimbMove(localRight, localForward);
        }
    }

    void PlayerComponent::SetJumpPressed() noexcept
    {
        if (m_input != nullptr)
        {
            m_input->SetJumpPressed();
        }
    }

    void PlayerComponent::SetReleaseLedgePressed() noexcept
    {
        if (m_input != nullptr)
        {
            m_input->SetReleaseLedgePressed();
        }
    }

    void PlayerComponent::SetJumpHeld(bool held) noexcept
    {
        if (m_input != nullptr)
        {
            m_input->SetJumpHeld(held);
        }
    }

    void PlayerComponent::SetMaxSpeedScale(float scale) noexcept
    {
        // 非数を入れると MaxSpeed() との比較が偽になり、突進明けに水平の速さが切られない
        if (!std::isfinite(scale))
        {
            return;
        }
        m_maxSpeedScale = scale;
    }

    bool PlayerComponent::IsBodySlamming() const noexcept
    {
        return m_states != nullptr && m_states->IsCurrent<BodySlamPlayerState>();
    }

    float PlayerComponent::BodySlamProgress01() const noexcept
    {
        if (!IsBodySlamming() || !(m_slam.distanceTarget > 0.0f))
        {
            return 0.0f;
        }
        return NS::Core::Clamp(m_slam.travelled / m_slam.distanceTarget, 0.0f, 1.0f);
    }

    bool PlayerComponent::IsRebounding() const noexcept
    {
        return m_states != nullptr && m_states->IsCurrent<ReboundPlayerState>();
    }

    void PlayerComponent::ResetState() noexcept
    {
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        if (m_input != nullptr)
        {
            m_input->ResetMovementInput();
        }
        m_prevJumpHeld = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        SetGrounded(false);
        m_ledgeTopY = 0.0f;
        m_ledgeFaceNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_ledgeMantleTimer = 0.0f;
        m_facingDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_lastMoveDistance = 0.0f;
        m_request.bufferRemaining = 0.0f;
        m_request.spent = false;
        m_slam.isTap = false;
        m_request.charge01 = 0.0f;
        m_request.dir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_request.hasDir = false;
        m_slam.charge01 = 0.0f;
        m_slam.travelled = 0.0f;
        m_slam.distanceTarget = 0.0f;
        m_slam.justStarted = false;
        m_slam.dir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_slam.startDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_rebound.direction = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_actor->ForgetHoming();
        // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
        // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
        m_curled = false;
        SetSphereShape(false);
        m_bodySlamHeld = false;
        m_slam.wasSlamming = false;

        if (m_states != nullptr)
        {
            m_states->Reset();
        }
    }

    void PlayerComponent::OnUpdate()
    {
        NS::Game::Entity::EntityComponent::OnUpdate();
    }

    NS::Obj::StateMachine<::Player>* PlayerComponent::States() const noexcept
    {
        return m_states;
    }

    void PlayerComponent::HandleMovement(float dt) noexcept
    {
        // 壁に当たった後の速度からは面へ向かう分が抜ける。掴む向きに使うので、抜ける前の向きを覚える
        NS::Core::Vector3 target{};
        if (NS::Core::TryNormalizeHorizontal(LateralVelocity(), target))
        {
            // 一定の速さで回す。速さの理由は m_turnSpeed の欄
            NS::Core::Vector3 current{};
            if (Tuning().m_turnSpeed > 0.0f && NS::Core::TryNormalizeHorizontal(m_facingDir, current))
            {
                const float maxTurn = NS::Core::ToRadians(NS::Core::Degrees{Tuning().m_turnSpeed * dt}).value;
                m_facingDir = TurnHorizontalToward(current, target, maxTurn);
            }
            else
            {
                m_facingDir = target;
            }
        }

        const NS::Core::Vector3 before = RootTransform().Position();
        NS::Game::Entity::EntityComponent::Move(dt, Tuning().m_maxStepHeight);
        const NS::Core::Vector3 delta = RootTransform().Position() - before;
        m_lastMoveDistance = delta.Length();
        SyncGroundState();
        m_actor->AdvanceBodySlamTravel(delta);
        m_slam.wasSlamming = IsBodySlamming();
    }

    void PlayerComponent::SyncGroundState() noexcept
    {
        if (!WasGrounded() && IsGrounded())
        {
            m_jumpsRemaining = 1;
        }

        if (IsGrounded())
        {
            m_coyoteTimer = Tuning().m_coyoteTime;
            // 着地のフレームだけで戻すと、接地したまま走り抜けた突進の後に次が出せない
            m_request.spent = false;
        }
    }

    void PlayerComponent::OnStepSkipped()
    {
        if (m_input != nullptr)
        {
            m_input->ConsumePressed();
        }
        m_prevJumpHeld = Input().JumpHeld();
    }

    NS_CLASS(PlayerComponent)
} // namespace NS::Game::Player
