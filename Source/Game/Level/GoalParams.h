#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Component.h"

namespace NS::Game::Level
{
    class GoalParams : public NS::Obj::Component
    {
    public:
        [[nodiscard]] float FadeOutSeconds() const noexcept
        {
            if (!NS::IsNonNegativeFinite(m_fadeOutSeconds))
            {
                return 0.0f;
            }
            return m_fadeOutSeconds;
        }

        [[nodiscard]] float FadeInSeconds() const noexcept
        {
            if (!NS::IsNonNegativeFinite(m_fadeInSeconds))
            {
                return 0.0f;
            }
            return m_fadeInSeconds;
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
