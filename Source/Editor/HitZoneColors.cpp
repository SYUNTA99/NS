#include "Editor/HitZoneColors.h"

#include <array>

namespace NS::Editor
{
    namespace
    {
        struct HitZoneColorRow
        {
            GL::Level::HitTier tier;
            float red;
            float green;
            float blue;
        };

        constexpr std::array<HitZoneColorRow, 2> k_HitZoneColors{{
            {GL::Level::HitTier::Center, 1.0f, 0.25f, 0.25f},
            {GL::Level::HitTier::Wide, 0.25f, 0.45f, 1.0f},
        }};
    } // namespace

    NS::Color HitZoneColor(GL::Level::HitTier tier) noexcept
    {
        for (const HitZoneColorRow& row : k_HitZoneColors)
        {
            if (row.tier == tier)
            {
                return NS::Color{row.red, row.green, row.blue, 1.0f};
            }
        }
        // 番号から作った段や欠番が来ても赤に見せない
        if (tier != GL::Level::HitTier::Wide)
        {
            return HitZoneColor(GL::Level::HitTier::Wide);
        }
        // 外れの行まで消した表。どの段とも見分けられる白
        return NS::Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
} // namespace NS::Editor
