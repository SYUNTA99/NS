#pragma once

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"

namespace GL::Level
{
    class FollowCamera : public NS::Obj::Actor
    {
    public:
        NS_REFLECT_BEGIN(FollowCamera, NS::Obj::Actor)
        NS_REFLECT_SUBOBJ(m_vcam, "Vcam")
        NS_REFLECT_END()
        [[nodiscard]] NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Camera; }
        [[nodiscard]] NS::Obj::ThirdPersonFollow& Vcam() noexcept { return *m_vcam; }
        [[nodiscard]] const NS::Obj::ThirdPersonFollow& Vcam() const noexcept { return *m_vcam; }

    protected:
        void OnInit() override;
        //! 追う相手の状態を読んで仮想カメラへ渡す
        void ObserveStep() override;
        void BodyStep() override;
        void OnDisappear() noexcept override;

    private:
        std::unique_ptr<NS::Obj::ThirdPersonFollow> m_vcam;
    };
} // namespace GL::Level
