#pragma once

#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        class FollowCameraActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_BEGIN(FollowCameraActor, NS::SceneActor)
            NS_CLASSOBJ(m_vcam, "Vcam")
            NS_REFLECT_END()
            [[nodiscard]] NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Camera; }
            [[nodiscard]] NS::ThirdPersonFollow& Vcam() noexcept { return *m_vcam; }
            [[nodiscard]] const NS::ThirdPersonFollow& Vcam() const noexcept { return *m_vcam; }
            void OnEndPlay() override;
            //! 「追従対象」の指す相手を取得し直して仮想カメラへ渡す
            void ResolveActorRefs() override;

        protected:
            void OnInit() override;
            //! 仮想カメラを置いた向きへ戻す
            void OnStart() override;
            //! 仮想カメラは入れたまま、出ている間だけ選べる
            void OnAppear() override;
            //! 追う相手の状態を読んで仮想カメラへ渡す
            void ObserveStep() override;
            void BodyStep() override;
            void OnDisappear() noexcept override;

        private:
            std::unique_ptr<NS::ThirdPersonFollow> m_vcam;
        };
    } // namespace Level
} // namespace GL
