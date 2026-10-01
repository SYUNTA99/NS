#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"

namespace NS::Game::Level
{
    class FollowCamera : public NS::Obj::Actor
    {
    public:
        FollowCamera() noexcept;
        void ForEachPart(const PartVisitor& visitor) const override;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "FollowCamera"; }
        NS_REFLECT_NONE(FollowCamera, NS::Obj::Actor)
        [[nodiscard]] NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Camera; }
        void Update() override;
        [[nodiscard]] NS::Obj::ThirdPersonFollow& Vcam() noexcept { return m_vcam; }
        [[nodiscard]] const NS::Obj::ThirdPersonFollow& Vcam() const noexcept { return m_vcam; }

    protected:
        void OnKill() noexcept override;

    private:
        NS::Obj::ThirdPersonFollow m_vcam;
    };
} // namespace NS::Game::Level
