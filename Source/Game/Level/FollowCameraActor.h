#pragma once

#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! 追従カメラ (ThirdPersonFollow) を置き、出ている間だけ選べる物にする
        //! 仮想カメラを進めるのは CameraManager の OnTick で、このアクターは更新の関数を持たない
        class FollowCameraActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_BEGIN(FollowCameraActor, NS::SceneActor)
            NS_CLASSOBJ(m_vcam, "Vcam")
            NS_REFLECT_END()
            [[nodiscard]] NS::ThirdPersonFollow& Vcam() noexcept { return *m_vcam; }
            [[nodiscard]] const NS::ThirdPersonFollow& Vcam() const noexcept { return *m_vcam; }
            void OnEndPlay() override;
            //! 「追従対象」の指す相手を取得し直し、相手が状態を渡す口と一緒に仮想カメラへ渡す
            void ResolveActorRefs() override;

        protected:
            void OnInit() override;
            //! 仮想カメラを置いた向きへ戻す
            void OnStart() override;
            //! 仮想カメラは入れたまま、出ている間だけ選べる
            void OnAppear() override;
            void OnDisappear() noexcept override;

        private:
            std::unique_ptr<NS::ThirdPersonFollow> m_vcam;
        };
    } // namespace Level
} // namespace GL
