#pragma once

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"

namespace NS::Game::Level
{
    class FollowCamera : public NS::Obj::Actor
    {
    public:
        FollowCamera() noexcept;
        void ForEachSubObj(const SubObjVisitor& visitor) const override;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_NONE(FollowCamera, NS::Obj::Actor)
        [[nodiscard]] NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Camera; }
        [[nodiscard]] NS::Obj::ThirdPersonFollow& Vcam() noexcept { return m_vcam; }
        [[nodiscard]] const NS::Obj::ThirdPersonFollow& Vcam() const noexcept { return m_vcam; }

    protected:
        //! 追う相手の状態を読んで仮想カメラへ渡す
        void ObserveStep() override;
        //! 仮想カメラを 1 歩進める
        void BodyStep() override;
        void OnKill() noexcept override;

    private:
        NS::Obj::ThirdPersonFollow m_vcam;
    };
} // namespace NS::Game::Level
