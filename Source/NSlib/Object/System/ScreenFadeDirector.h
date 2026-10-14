#pragma once

#include "NSlib/Graphics/ScreenFade.h"
#include "NSlib/Object/System/SceneSystem.h"

namespace NS::Obj
{
    //! @brief シーンの暗転 ScreenFade を持ち、UI の段で固定フレーム 1 回分進める
    //! @details UI の段は世界の速さに依らず、自機以外の止めの間も回るので、暗転は実時間で進む。
    //! 描くのは SceneRenderer で、全 UI の後に重ねる
    class ScreenFadeDirector final : public SceneSystem
    {
    public:
        [[nodiscard]] NS::Gfx::ScreenFade& Fade() noexcept { return m_fade; }
        [[nodiscard]] const NS::Gfx::ScreenFade& Fade() const noexcept { return m_fade; }

        void OnTick() override;

    private:
        NS::Gfx::ScreenFade m_fade;
    };
} // namespace NS::Obj
