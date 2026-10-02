#include "Editor/HitZoneColors.h"
#include "Game/Level/HitTier.h"
#include "Runtime/Core/Math.h"

#include <gtest/gtest.h>

// エディタの面と当たりの印に使う、段の名前で引く色の表

namespace
{
    using NS::Game::Level::HitTier;

    bool SameColor(const NS::Core::Color& a, const NS::Core::Color& b)
    {
        return a.R() == b.R() && a.G() == b.G() && a.B() == b.B() && a.A() == b.A();
    }
} // namespace

// 真ん中は赤、外れは青。本人の呼び方のまま
TEST(HitZoneColorsTest, CenterIsRedAndWideIsBlue)
{
    const NS::Core::Color center = NS::Editor::HitZoneColor(HitTier::Center);
    const NS::Core::Color wide = NS::Editor::HitZoneColor(HitTier::Wide);

    EXPECT_GT(center.R(), center.G() + 0.5f);
    EXPECT_GT(center.R(), center.B() + 0.5f);
    EXPECT_GT(wide.B(), wide.R() + 0.5f);
    EXPECT_GT(wide.B(), wide.G() + 0.2f);
    EXPECT_FLOAT_EQ(center.A(), 1.0f);
    EXPECT_FLOAT_EQ(wide.A(), 1.0f);
}

// 表に行の無い段は、外れの行の色で描く。番号から作った段や欠番の 1 が来ても赤に見せない
TEST(HitZoneColorsTest, TierWithoutARowUsesTheWideRow)
{
    const NS::Core::Color wide = NS::Editor::HitZoneColor(HitTier::Wide);

    EXPECT_TRUE(SameColor(NS::Editor::HitZoneColor(static_cast<HitTier>(1)), wide));
    EXPECT_TRUE(SameColor(NS::Editor::HitZoneColor(static_cast<HitTier>(3)), wide));
}
