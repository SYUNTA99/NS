#include "Game/Level/FollowCameraFeed.h"

#include "Game/Entity/EntityComponent.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <algorithm>

namespace NS::Game::Level
{
    namespace
    {
        // 溜め量は放した後も放した時の値を返し続けるので、押していないフレームは 0 を渡す
        [[nodiscard]] NS::Obj::FollowChargeDesc MakeFollowCharge(const CollisionInput& input) noexcept
        {
            const ImpactInputJudge& judge = input.Judge();
            const bool held = judge.IsHeld();
            float charge01 = 0.0f;
            if (held)
            {
                charge01 = judge.Charge01();
            }
            SlamLineTarget aim{};
            const bool hasAimTarget = input.TryGetAimTarget(aim);
            NS::Core::Vector3 center{};
            float radius = 0.0f;
            if (hasAimTarget)
            {
                center = NS::Core::Vector3{aim.bounds.Center.x, aim.bounds.Center.y, aim.bounds.Center.z};
                radius = std::max({aim.bounds.Extents.x, aim.bounds.Extents.y, aim.bounds.Extents.z});
            }
            return NS::Obj::FollowChargeDesc{
                .charge01 = charge01,
                .held = held,
                .hasAimTarget = hasAimTarget,
                .aimTargetCenter = center,
                .aimTargetRadius = radius,
            };
        }
    } // namespace

    // Respawner のやり直し (LateUpdate + 10) が済んだ後、
    // ThirdPersonFollow (LateUpdate + 50) が読む前に渡す
    FollowCameraFeed::FollowCameraFeed() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate + 40) {}

    void FollowCameraFeed::OnStart()
    {
        if (Owner() != nullptr)
        {
            m_follow = Owner()->FindComponent<NS::Obj::ThirdPersonFollow>();
        }
        else
        {
            m_follow = nullptr;
        }
    }

    void FollowCameraFeed::OnUpdate()
    {
        if (m_follow == nullptr || Owner()->OwningScene() == nullptr)
        {
            return;
        }

        // 追う相手は控えず毎フレーム引き直す。控えると、消された相手を指したまま次のフレームへ持ち越す
        NS::Obj::GameObject* target = Owner()->OwningScene()->Objects().FindObject(m_follow->TargetRef());
        if (target == nullptr)
        {
            return;
        }
        const NS::Game::Entity::EntityComponent* entity = target->FindComponent<NS::Game::Entity::EntityComponent>();
        if (entity == nullptr)
        {
            return;
        }
        m_follow->SetFollowMotion(entity->IsGrounded(), entity->Velocity());
        // 当たりの足元に立ち姿のカプセルを立てた時の中心を見る。玉の間は根が立ち姿の半長ぶん下がっているので、
        // 根を見ると押すたびに画面が 1 フレームで半長ぶん沈み、解けると跳ね上がる
        m_follow->SetTargetHeightOffset(entity->StandingHalfHeight() - entity->CapsuleHalfHeight());
        const NS::Game::Player::PlayerComponent* player = target->FindComponent<NS::Game::Player::PlayerComponent>();
        if (player != nullptr)
        {
            const NS::Obj::FollowReboundDesc rebound{
                .rebounding = player->IsRebounding(),
                .slamDirection = player->BodySlamStartDirection(),
            };
            if (!m_follow->SetFollowRebound(rebound))
            {
                NS_LOG_WARN(Game,
                            "突進の向きが壊れていて、反動をカメラへ渡さなかった: ({}, {}, {})",
                            rebound.slamDirection.x,
                            rebound.slamDirection.y,
                            rebound.slamDirection.z);
            }
        }

        const CollisionInput* input = target->FindComponent<CollisionInput>();
        if (input == nullptr)
        {
            return;
        }
        if (!m_follow->SetFollowCharge(MakeFollowCharge(*input)))
        {
            NS_LOG_WARN(Game, "溜めの状態が壊れていて、追従カメラへ渡さなかった: 溜め量 {}", input->Judge().Charge01());
        }
    }

    void FollowCameraFeed::OnEndPlay()
    {
        if (m_follow != nullptr)
        {
            m_follow->SetTargetHeightOffset(0.0f);
            m_follow->ClearCharge();
            m_follow->ClearRebound();
        }
    }

    NS_CLASS(FollowCameraFeed)
} // namespace NS::Game::Level
