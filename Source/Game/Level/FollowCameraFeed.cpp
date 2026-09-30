#include "Game/Level/FollowCameraFeed.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"


namespace NS::Game::Level
{
    // 進行役のやり直し (LateUpdate + 10) が済んだ後、
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
        NS::Obj::Actor* target = Owner()->OwningScene()->Objects().FindObject(m_follow->TargetRef());
        if (target == nullptr)
        {
            return;
        }
        // 相手の部品は読まない。追われる側が窓口で答えた状態だけを使う
        const NS::Obj::ICameraTarget* cameraTarget = target->GetCameraTarget();
        if (cameraTarget == nullptr)
        {
            return;
        }
        const NS::Obj::CameraTargetState state = cameraTarget->GetCameraTargetState();
        m_follow->SetFollowMotion(state.grounded, state.velocity);
        m_follow->SetTargetHeightOffset(state.heightOffset);
        if (state.hasRebound && !m_follow->SetFollowRebound(state.rebound))
        {
            NS_LOG_WARN(Game,
                        "突進の向きが壊れていて、反動をカメラへ渡さなかった: ({}, {}, {})",
                        state.rebound.slamDirection.x,
                        state.rebound.slamDirection.y,
                        state.rebound.slamDirection.z);
        }
        if (state.hasCharge && !m_follow->SetFollowCharge(state.charge))
        {
            NS_LOG_WARN(Game, "溜めの状態が壊れていて、追従カメラへ渡さなかった: 溜め量 {}", state.charge.charge01);
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
