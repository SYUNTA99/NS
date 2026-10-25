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
            CreateClassObj(m_vcam);
            NS::InitVirtualCamera(this, m_vcam.get());
        }

        void FollowCameraActor::OnStart()
        {
            NS::SceneActor::OnStart();
            m_vcam->ResetPose();
        }

        void FollowCameraActor::ResolveActorRefs()
        {
            m_vcam->SetTarget(OwningScene()->Actors().FindActor(m_vcam->TargetRef()));
        }

        void FollowCameraActor::OnAppear()
        {
            NS::SceneActor::OnAppear();
            if (IsAppeared())
            {
                NS::EnableVirtualCamera(this, m_vcam.get());
            }
        }

        void FollowCameraActor::ObserveStep()
        {
            const ::PlayerActor* player = NS::Cast<::PlayerActor>(m_vcam->TargetActor());
            if (player == nullptr)
            {
                return;
            }
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

        void FollowCameraActor::BodyStep()
        {
            // clang-format off
            if (m_vcam) { m_vcam->Update(this, this); }
            // clang-format on
        }

        void FollowCameraActor::OnDisappear() noexcept
        {
            m_vcam->SetTargetHeightOffset(0.0f);
            m_vcam->ClearCharge();
            m_vcam->ClearRebound();
            NS::DisableVirtualCamera(this, m_vcam.get());
            NS::SceneActor::OnDisappear();
        }

        void FollowCameraActor::OnEndPlay()
        {
            NS::SceneActor::OnEndPlay();
            NS::EndVirtualCamera(this, m_vcam.get());
        }
    } // namespace Level
} // namespace GL
