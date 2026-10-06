#pragma once

#include "NSlib/Object/Component.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    class GoalParams : public NS::Obj::Component
    {
    public:
        [[nodiscard]] float FadeOutSeconds() const noexcept
        {
            if (!std::isfinite(m_fadeOutSeconds))
            {
                return 0.0f;
            }
            return std::max(m_fadeOutSeconds, 0.0f);
        }

        [[nodiscard]] float FadeInSeconds() const noexcept
        {
            if (!std::isfinite(m_fadeInSeconds))
            {
                return 0.0f;
            }
            return std::max(m_fadeInSeconds, 0.0f);
        }

        NS_REFLECT_BEGIN(GoalParams, NS::Obj::Component)
        NS_REFLECT_FIELD(m_fadeOutSeconds, "クリアの暗転秒")
        NS_REFLECT_FIELD(m_fadeInSeconds, "クリアの明転秒")
        NS_REFLECT_END()

    private:
        float m_fadeOutSeconds = 0.4f;
        float m_fadeInSeconds = 0.4f;
    };
} // namespace NS::Game::Level
