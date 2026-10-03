#include "Game/StandaloneLayer.h"

#include <gtest/gtest.h>

// 単体の遊びの外枠が、プレイ中の Esc を 2 段階で応じることを縛る

TEST(StandaloneLayer, FirstEscapeReleasesTheCursor)
{
    EXPECT_EQ(StandaloneLayer::ResolveEscape(false), StandaloneLayer::EscapeResponse::ReleaseCursor);
}

TEST(StandaloneLayer, EscapeWithTheCursorOutQuits)
{
    EXPECT_EQ(StandaloneLayer::ResolveEscape(true), StandaloneLayer::EscapeResponse::Quit);
}
