#include "Game/Level/FollowCamera.h"

#include "Game/Player.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"

namespace GL::Level
{
    NS_PLACEABLE(FollowCamera, "追従カメラ")
    void FollowCamera::OnInit()
    {
        (void)CreateSubObj(m_vcam);
    }

    // 仮想カメラはプレイの間ずっと候補に入れておく。消えている間は、カメラの管理役が IsActive を見て飛ばす
    void FollowCamera::OnStart()
    {
        NS::Obj::Actor::OnStart();
        NS::Obj::RegisterVirtualCamera(this, m_vcam.get());
        m_vcam->ResetPose();
    }

    void FollowCamera::ObserveStep()
    {
        if (!m_vcam->IsActive())
        {
            return;
        }
        if (NS::Obj::Scene* scene = OwningScene())
        {
            NS::Obj::Actor* target = scene->Objects().FindObject(m_vcam->TargetRef());
            if (target != nullptr)
            {
                if (const ::Player* player = NS::Obj::Cast<::Player>(target))
                {
                    const GL::Player::CameraTargetState state = player->GetCameraTargetState();
                    m_vcam->SetFollowMotion(state.grounded, state.velocity);
                    m_vcam->SetTargetHeightOffset(state.heightOffset);
                    m_vcam->SetFollowFramingHeld(state.framingHeld);
                    if (state.hasRebound && !m_vcam->SetFollowRebound(state.rebound))
                    {
                        NS_LOG_WARN(Game,
                                    "突進の向きが壊れていて、反動をカメラへ渡さなかった: ({}, {}, {})",
                                    state.rebound.slamDirection.x,
                                    state.rebound.slamDirection.y,
                                    state.rebound.slamDirection.z);
                    }
                    if (state.hasCharge && !m_vcam->SetFollowCharge(state.charge))
                    {
                        NS_LOG_WARN(
                            Game, "溜めの状態が壊れていて、追従カメラへ渡さなかった: 溜め量 {}", state.charge.charge01);
                    }
                }
            }
        }
    }

    void FollowCamera::BodyStep()
    {
        TickSubObj(m_vcam.get());
    }

    void FollowCamera::OnDisappear() noexcept
    {
        m_vcam->SetTargetHeightOffset(0.0f);
        m_vcam->ClearCharge();
        m_vcam->ClearRebound();
        NS::Obj::Actor::OnDisappear();
    }

    void FollowCamera::OnEndPlay()
    {
        NS::Obj::Actor::OnEndPlay();
        NS::Obj::UnregisterVirtualCamera(this, m_vcam.get());
    }
} // namespace GL::Level
