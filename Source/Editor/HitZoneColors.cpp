#include "Editor/HitZoneColors.h"

namespace NS::Editor
{
    // 当たり判定の線の緑・カメラの黄・選択の橙と重ならない色にする
    // 真ん中は一番嬉しい当たりなので目を引く赤、外れは目立たない灰色
    NS::Core::Color HitZoneColor(NS::Game::Level::HitTier tier) noexcept
    {
        if (tier == NS::Game::Level::HitTier::Center)
        {
            return NS::Core::Color{1.0f, 0.25f, 0.25f, 1.0f};
        }
        if (tier == NS::Game::Level::HitTier::Near)
        {
            return NS::Core::Color{0.3f, 0.7f, 1.0f, 1.0f};
        }
        return NS::Core::Color{0.6f, 0.6f, 0.6f, 1.0f};
    }

    NS::Core::Color HitZoneWarningColor() noexcept
    {
        return NS::Core::Color{1.0f, 0.2f, 1.0f, 1.0f};
    }

    // 当たり判定の緑 (0.35, 1, 0.45) の明るさを半分ほどに落とす。緑の仲間と読めて、ぶつかる物とは見分けられる
    NS::Core::Color HitZoneCandidateColor() noexcept
    {
        return NS::Core::Color{0.18f, 0.5f, 0.22f, 1.0f};
    }

    NS::Core::Color HitZoneRingColor(NS::Game::Level::HitTier tier, bool orderBroken) noexcept
    {
        if (orderBroken)
        {
            return HitZoneWarningColor();
        }
        return HitZoneColor(tier);
    }
} // namespace NS::Editor
