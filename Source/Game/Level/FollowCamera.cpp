#include "Game/Level/FollowCamera.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/CameraTarget.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    NS_PLACEABLE(FollowCamera, "追従カメラ")

    void FollowCamera::OnInit()
    {
        m_vcam = CreateSubObj<NS::Obj::ThirdPersonFollow>("Vcam");
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
                if (const NS::Obj::ICameraTarget* cameraTarget = target->GetCameraTarget())
                {
                    const NS::Obj::CameraTargetState state = cameraTarget->GetCameraTargetState();
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
        TickSubObj(m_vcam);
    }

    void FollowCamera::OnKill() noexcept
    {
        m_vcam->SetTargetHeightOffset(0.0f);
        m_vcam->ClearCharge();
        m_vcam->ClearRebound();
        NS::Obj::Actor::OnKill();
    }
} // namespace NS::Game::Level
