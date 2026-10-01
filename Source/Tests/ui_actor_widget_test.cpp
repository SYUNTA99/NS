#include "Game/Level/ScreenFade.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/UI/ColorRect.h"

#include <gtest/gtest.h>

TEST(UIActorWidgets, FadeOwnsAFullScreenWidgetAndKeepsTheExistingAlphaTiming)
{
    NS::Obj::Scene scene;
    NS::Game::Level::ScreenFade fade;
    EXPECT_TRUE(fade.Widgets().Root().HasChildren());
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.0f);
    fade.Open(scene);
    fade.Widgets().Layout(1600.0f, 900.0f);
    EXPECT_NEAR(fade.Widgets().Root().LayoutRect().width, 1920.0f, 0.0001f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().LayoutRect().height, 1080.0f);
    fade.BeginOut(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.25f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), fade.Alpha());
    fade.Advance(0.3f);
    EXPECT_TRUE(fade.IsBlack());
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 1.0f);
    fade.BeginIn(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.75f);
    fade.Cancel();
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.0f);
    fade.Close();
}

TEST(UIActorWidgets, ZeroDurationFadeChangesTheWidgetImmediately)
{
    NS::Game::Level::ScreenFade fade;
    fade.BeginOut(0.0f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 1.0f);
    fade.BeginIn(0.0f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.0f);
}
