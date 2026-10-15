#pragma once

#include "NSlib/Object/SceneActor.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"

namespace GL::Level
{
    class FollowCameraActor : public NS::Obj::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(FollowCameraActor, NS::Obj::SceneActor)
        NS_REFLECT_SUBOBJ(m_vcam, "Vcam")
        NS_REFLECT_END()
        [[nodiscard]] NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Camera; }
        [[nodiscard]] NS::Obj::ThirdPersonFollow& Vcam() noexcept { return *m_vcam; }
        [[nodiscard]] const NS::Obj::ThirdPersonFollow& Vcam() const noexcept { return *m_vcam; }
        void OnEndPlay() override;

    protected:
        void OnInit() override;
        //! 仮想カメラを登録し、置いた向きへ戻す
        void OnStart() override;
        //! 追う相手の状態を読んで仮想カメラへ渡す
        void ObserveStep() override;
        void BodyStep() override;
        void OnDisappear() noexcept override;

    private:
        std::unique_ptr<NS::Obj::ThirdPersonFollow> m_vcam;
    };
} // namespace GL::Level
