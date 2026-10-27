#pragma once

#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/Gfx/Render/ScreenFade.h"

namespace NS
{
    //! シーンの暗転 ScreenFade を持ち、UI の段で固定フレーム 1 回分進める
    //! UI の段は世界の速さに依らず、ほかを止めている間 (Scene::HoldOthers) も回るので、暗転は実時間で進む。
    //! 描くのは SceneRenderer で、全 UI の後に重ねる
    class ScreenFadeDirector final : public SceneSystem
    {
    public:
        [[nodiscard]] Gfx::ScreenFade& Fade() noexcept { return m_fade; }
        [[nodiscard]] const Gfx::ScreenFade& Fade() const noexcept { return m_fade; }

        void OnTick() override;

    private:
        Gfx::ScreenFade m_fade;
    };
} // namespace NS
