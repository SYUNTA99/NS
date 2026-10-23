#include "Game/Level/FollowCameraActor.h"

#include "Game/PlayerActor.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"

namespace GL
{
    namespace Level
    {
        NS_PLACEABLE(FollowCameraActor, "追従カメラ")
        void FollowCameraActor::OnInit()
        {
            CreateClassObj(m_vcam, this);
        }

        // 仮想カメラはプレイの間ずっと候補に入れておく。消えている間は、カメラの管理役が VirtualCamera::IsOwnerAppeared を見て飛ばす
        void FollowCameraActor::OnStart()
        {
            NS::SceneActor::OnStart();
            NS::RegisterVirtualCamera(this, m_vcam.get());
            m_vcam->ResetPose();
        }

        void FollowCameraActor::ObserveStep()
        {
            if (NS::Scene* scene = OwningScene())
            {
                NS::SceneActor* target = scene->Actors().FindActor(m_vcam->TargetRef());
                if (target != nullptr)
                {
                    if (const ::PlayerActor* player = NS::Cast<::PlayerActor>(target))
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
                            NS_LOG_WARN(Game,
                                        "溜めの状態が壊れていて、追従カメラへ渡さなかった: 溜め量 {}",
                                        state.charge.charge01);
                        }
                    }
                }
            }
        }

        void FollowCameraActor::BodyStep()
        {
            // clang-format off
            if (m_vcam) { m_vcam->Update(); }
            // clang-format on
        }

        void FollowCameraActor::OnDisappear() noexcept
        {
            m_vcam->SetTargetHeightOffset(0.0f);
            m_vcam->ClearCharge();
            m_vcam->ClearRebound();
            NS::SceneActor::OnDisappear();
        }

        void FollowCameraActor::OnEndPlay()
        {
            NS::SceneActor::OnEndPlay();
            NS::UnregisterVirtualCamera(this, m_vcam.get());
        }
    } // namespace Level
} // namespace GL
