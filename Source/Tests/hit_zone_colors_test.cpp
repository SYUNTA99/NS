#include <Editor/HitZoneColors.h>
#include <Game/Level/HitTier.h>
#include <Runtime/Core/Math.h>

#include <gtest/gtest.h>

namespace
{
    using NS::Game::Level::HitTier;

    bool SameColor(const NS::Core::Color& a, const NS::Core::Color& b)
    {
        return a.R() == b.R() && a.G() == b.G() && a.B() == b.B() && a.A() == b.A();
    }
} // namespace

// 段ごとに色が違い、警告の色はどの段とも違う
TEST(HitZoneColorsTest, EachTierAndTheWarningHaveTheirOwnColor)
{
    const NS::Core::Color center = NS::Editor::HitZoneColor(HitTier::Center);
    const NS::Core::Color wide = NS::Editor::HitZoneColor(HitTier::Wide);
    const NS::Core::Color warning = NS::Editor::HitZoneWarningColor();

    EXPECT_FALSE(SameColor(center, wide));
    EXPECT_FALSE(SameColor(warning, center));
    EXPECT_FALSE(SameColor(warning, wide));
}

// 真ん中は赤、外れは青。本人の呼び方のまま
TEST(HitZoneColorsTest, TiersAreRedAndBlue)
{
    const NS::Core::Color center = NS::Editor::HitZoneColor(HitTier::Center);
    const NS::Core::Color wide = NS::Editor::HitZoneColor(HitTier::Wide);

    EXPECT_GT(center.R(), center.G() + 0.5f);
    EXPECT_GT(center.R(), center.B() + 0.5f);
    EXPECT_GT(wide.B(), wide.R() + 0.5f);
    EXPECT_GT(wide.B(), wide.G() + 0.2f);
}
