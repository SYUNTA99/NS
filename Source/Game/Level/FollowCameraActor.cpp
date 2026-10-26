#include "Game/Level/FollowCameraActor.h"

#include "Game/PlayerActor.h"
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
            const NS::SceneActor* target = OwningScene()->Actors().FindActor(m_vcam->TargetRef());
            // 状態を渡す相手は今は自機だけ。実行時型情報を切っているので、型で確かめてから口へ変える
            const NS::IUseCameraTarget* state = NS::Cast<::PlayerActor>(target);
            m_vcam->SetTarget(target, state);
        }

        void FollowCameraActor::OnAppear()
        {
            NS::SceneActor::OnAppear();
            if (IsAppeared())
            {
                NS::EnableVirtualCamera(this, m_vcam.get());
            }
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
