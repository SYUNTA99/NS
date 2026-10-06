#include "Editor/HitZoneColors.h"

#include <array>

namespace NS::Editor
{
    namespace
    {
        struct HitZoneColorRow
        {
            NS::Game::Level::HitTier tier;
            float red;
            float green;
            float blue;
        };

        // 真ん中は赤、外れは青 (2026-09-29 本人の呼び方)。警告の黄は惜しいの段と一緒に消した
        constexpr std::array<HitZoneColorRow, 2> k_HitZoneColors{{
            {NS::Game::Level::HitTier::Center, 1.0f, 0.25f, 0.25f},
            {NS::Game::Level::HitTier::Wide, 0.25f, 0.45f, 1.0f},
        }};
    } // namespace

    NS::Color HitZoneColor(NS::Game::Level::HitTier tier) noexcept
    {
        for (const HitZoneColorRow& row : k_HitZoneColors)
        {
            if (row.tier == tier)
            {
                return NS::Color{row.red, row.green, row.blue, 1.0f};
            }
        }
        // 番号から作った段や欠番が来ても赤に見せない
        if (tier != NS::Game::Level::HitTier::Wide)
        {
            return HitZoneColor(NS::Game::Level::HitTier::Wide);
        }
        // 外れの行まで消した表。どの段とも見分けられる白
        return NS::Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
} // namespace NS::Editor
