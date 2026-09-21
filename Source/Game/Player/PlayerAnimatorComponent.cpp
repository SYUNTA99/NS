#include "Game/Player/PlayerAnimatorComponent.h"

#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerStateManagerComponent.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Runtime/Object/Components/SkeletalAnimationComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>

namespace NS::Game::Player
{
    // 移動 (Update) と骨を回す SkeletalAnimationComponent (Update + 100) の間ならどこでもよい
    // 真ん中に置いて両側へ足せる幅を残す
    PlayerAnimatorComponent::PlayerAnimatorComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update + 50)
    {}

    void PlayerAnimatorComponent::OnStart()
    {
        NS::Object::Component::OnStart();

        if (Owner() == nullptr)
        {
            return;
        }

        m_player = Owner()->FindComponent<PlayerComponent>();
        m_states = Owner()->FindComponent<PlayerStateManagerComponent>();
        m_animation = Owner()->FindComponent<NS::Object::SkeletalAnimationComponent>();

        // 見た目は足元が原点なので、当たりの底へ下げるために子の配置物へ分けてある
        if (m_animation == nullptr)
        {
            for (NS::Object::GameObject* child : Owner()->Children())
            {
                m_animation = child->FindComponent<NS::Object::SkeletalAnimationComponent>();
                if (m_animation != nullptr)
                {
                    break;
                }
            }
        }
    }

    void PlayerAnimatorComponent::OnUpdate()
    {
        if (m_player == nullptr || m_animation == nullptr)
        {
            return;
        }

        const float lateralSpeed = m_player->LateralVelocity().Length();
        const std::string_view clip = ChooseClip(lateralSpeed);

        // 毎フレーム選び直すと SelectClip が再生時刻を 0 へ戻し、絵が 1 コマ目で止まる
        if (clip != m_appliedClip)
        {
            if (m_animation->SelectClip(clip))
            {
                m_appliedClip = clip;
            }
            else if (m_animation->SelectClip(m_idleClip))
            {
                m_appliedClip = m_idleClip;
            }
        }

        m_animation->SetSpeed(ChoosePlaybackSpeed(m_appliedClip, lateralSpeed));
    }

    std::string_view PlayerAnimatorComponent::ChooseClip(float lateralSpeed) const noexcept
    {
        // ぶら下がり中は HoldLedge が毎フレーム速度を 0 にする。状態を先に見ないと立ちか落ちるの絵になる
        if (m_states != nullptr && m_states->IsCurrent<LedgeHangingPlayerState>())
        {
            if (!m_ledgeHangClip.empty())
            {
                return m_ledgeHangClip;
            }
            return m_idleClip;
        }

        if (!m_player->IsGrounded())
        {
            if (m_player->VerticalVelocity() > 0.0f && !m_jumpClip.empty())
            {
                return m_jumpClip;
            }
            if (m_player->VerticalVelocity() <= 0.0f && !m_fallClip.empty())
            {
                return m_fallClip;
            }
            return m_idleClip;
        }

        if (lateralSpeed <= NS::Core::k_Epsilon)
        {
            return m_idleClip;
        }

        const float maxSpeed = m_player->MaxSpeed();
        if (maxSpeed > 0.0f && lateralSpeed >= maxSpeed * m_runBlendRatio && !m_runClip.empty())
        {
            return m_runClip;
        }
        if (!m_walkClip.empty())
        {
            return m_walkClip;
        }
        return m_idleClip;
    }

    float PlayerAnimatorComponent::ChoosePlaybackSpeed(std::string_view clip, float lateralSpeed) const noexcept
    {
        // 速さに合わせるのは足が地面を蹴る絵だけ
        if (clip != m_walkClip && clip != m_runClip)
        {
            return 1.0f;
        }

        const float maxSpeed = m_player->MaxSpeed();
        if (maxSpeed <= 0.0f)
        {
            return 1.0f;
        }
        return std::max(m_minPlaybackSpeed, lateralSpeed / maxSpeed);
    }

    NS_CLASS(PlayerAnimatorComponent)
} // namespace NS::Game::Player
