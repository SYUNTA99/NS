#include "Editor/HitZoneColors.h"

namespace NS::Editor
{
    // 真ん中は赤、外れは青 (2026-09-29 本人の呼び方)
    NS::Core::Color HitZoneColor(NS::Game::Level::HitTier tier) noexcept
    {
        if (tier == NS::Game::Level::HitTier::Center)
        {
            return NS::Core::Color{1.0f, 0.25f, 0.25f, 1.0f};
        }
        return NS::Core::Color{0.25f, 0.45f, 1.0f, 1.0f};
    }

    NS::Core::Color HitZoneWarningColor() noexcept
    {
        return NS::Core::Color{1.0f, 0.2f, 1.0f, 1.0f};
    }
} // namespace NS::Editor
