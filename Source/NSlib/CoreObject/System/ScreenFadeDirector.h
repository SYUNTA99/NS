#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/Render/ScreenFade.h"

namespace NS
{
    //! シーンの暗転 ScreenFade を持ち、固定フレーム 1 回分進める
    //! Scene は UI と同じく世界の速さに依らず、シーンが止まっている間 (Scene::RequestStopScene) も呼ぶので、
    //! 暗転は実時間で進む。描くのは SceneRenderer で、全 UI の後に重ねる
    class ScreenFadeDirector final : public NonCopyable
    {
    public:
        [[nodiscard]] Gfx::ScreenFade& Fade() noexcept { return m_fade; }
        [[nodiscard]] const Gfx::ScreenFade& Fade() const noexcept { return m_fade; }

        void OnTick();

    private:
        Gfx::ScreenFade m_fade;
    };
} // namespace NS
