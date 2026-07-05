#include "GameCore/Level/ClearFadeComponent.h"
#include "GameCore/Level/PlayFlowComponent.h"
#include "GameCore/LevelPlayScene.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::GameCore::Level;

/// Application 依存のない LevelPlayScene を器に、 暗転状態機械を dt 駆動で検証する
/// OnStart を呼ばないため ScreenFade は生成されず、 状態機械だけが進む

TEST(ClearFade, BeginStartsFadeOutAndAlphaRises)
{
    LevelPlayScene scene;
    auto& fade = scene.Director().Fade();

    EXPECT_FALSE(fade.IsFading());
    EXPECT_NEAR(fade.Alpha(), 0.0f, 1e-6f);

    fade.Begin();
    EXPECT_TRUE(fade.IsFading());

    fade.Advance(LevelNs::ClearFadeComponent::kFadeOutSeconds * 0.5f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 1e-4f);
}

TEST(ClearFade, FullBlackRestartsLevelThenFadesIn)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();
    auto& fade = scene.Director().Fade();

    flow.EnterPlay();
    // 体力を減らしておくと、 全回復していることが「全黒でリスタートが走った」証拠になる
    flow.Play().playerHealth = 3;

    fade.Begin();
    fade.Advance(LevelNs::ClearFadeComponent::kFadeOutSeconds);

    // 全黒に到達した瞬間にレベルを頭から再開し、 明転へ移る
    EXPECT_EQ(flow.Play().playerHealth, 8);
    EXPECT_TRUE(fade.IsFading());
    EXPECT_NEAR(fade.Alpha(), 1.0f, 1e-4f);

    fade.Advance(LevelNs::ClearFadeComponent::kFadeInSeconds);
    EXPECT_FALSE(fade.IsFading());
    EXPECT_NEAR(fade.Alpha(), 0.0f, 1e-6f);
}

TEST(ClearFade, ReentryIsIgnoredWhileFading)
{
    LevelPlayScene scene;
    auto& fade = scene.Director().Fade();

    fade.Begin();
    fade.Advance(LevelNs::ClearFadeComponent::kFadeOutSeconds * 0.5f);
    const float alphaBefore = fade.Alpha();

    // 進行中の再呼び出しは無視され、 タイマーも不透明度も巻き戻らない
    fade.Begin();
    EXPECT_NEAR(fade.Alpha(), alphaBefore, 1e-6f);

    fade.Advance(LevelNs::ClearFadeComponent::kFadeOutSeconds * 0.25f);
    EXPECT_GT(fade.Alpha(), alphaBefore);
}
