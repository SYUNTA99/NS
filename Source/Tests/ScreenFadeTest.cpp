#include "Game/Level/ScreenFadeComponent.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/OverlayRendererComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/World.h"

#include <gtest/gtest.h>

//! 暗転状態機械だけを dt で進めて検証する。 描画は呼ばない

namespace
{
    constexpr float k_OutSeconds = 0.4f;
    constexpr float k_InSeconds = 0.4f;
} // namespace

TEST(ScreenFade, BeginOutRaisesAlphaThenHoldsBlack)
{
    NS::Object::GameObject obj;
    auto& fade = *obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    EXPECT_FALSE(fade.IsFading());
    EXPECT_FALSE(fade.IsBlack());
    EXPECT_NEAR(fade.Alpha(), 0.0f, 1e-6f);

    fade.BeginOut(k_OutSeconds);
    EXPECT_TRUE(fade.IsFading());

    fade.Advance(k_OutSeconds * 0.5f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 1e-4f);

    // 暗転しきったら BeginIn まで全黒のまま。 進めても勝手に明転しない
    fade.Advance(k_OutSeconds * 0.5f);
    EXPECT_FALSE(fade.IsFading());
    EXPECT_TRUE(fade.IsBlack());
    EXPECT_NEAR(fade.Alpha(), 1.0f, 1e-6f);

    fade.Advance(1.0f);
    EXPECT_TRUE(fade.IsBlack());
    EXPECT_NEAR(fade.Alpha(), 1.0f, 1e-6f);
}

TEST(ScreenFade, BeginInLowersAlphaToTransparent)
{
    NS::Object::GameObject obj;
    auto& fade = *obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    fade.BeginOut(k_OutSeconds);
    fade.Advance(k_OutSeconds);
    ASSERT_TRUE(fade.IsBlack());

    fade.BeginIn(k_InSeconds);
    EXPECT_TRUE(fade.IsFading());
    EXPECT_FALSE(fade.IsBlack());

    fade.Advance(k_InSeconds * 0.5f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 1e-4f);

    fade.Advance(k_InSeconds * 0.5f);
    EXPECT_FALSE(fade.IsFading());
    EXPECT_FALSE(fade.IsBlack());
    EXPECT_NEAR(fade.Alpha(), 0.0f, 1e-6f);
}

TEST(ScreenFade, ReentryIsIgnoredWhileActive)
{
    NS::Object::GameObject obj;
    auto& fade = *obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    fade.BeginOut(k_OutSeconds);
    fade.Advance(k_OutSeconds * 0.5f);
    const float alphaBefore = fade.Alpha();

    // 演出中の呼び直しは無視され、 タイマーも不透明度も巻き戻らない
    fade.BeginOut(k_OutSeconds);
    EXPECT_NEAR(fade.Alpha(), alphaBefore, 1e-6f);

    // 全黒の保持中の BeginOut も何もしない
    fade.Advance(k_OutSeconds * 0.5f);
    ASSERT_TRUE(fade.IsBlack());
    fade.BeginOut(k_OutSeconds);
    EXPECT_TRUE(fade.IsBlack());
}

TEST(ScreenFade, CancelReturnsToTransparent)
{
    NS::Object::GameObject obj;
    auto& fade = *obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    fade.BeginOut(k_OutSeconds);
    fade.Advance(k_OutSeconds * 0.5f);
    ASSERT_TRUE(fade.IsFading());

    fade.Cancel();
    EXPECT_FALSE(fade.IsFading());
    EXPECT_FALSE(fade.IsBlack());
    EXPECT_NEAR(fade.Alpha(), 0.0f, 1e-6f);
}

TEST(ScreenFade, ZeroSecondsJumpsToEndState)
{
    NS::Object::GameObject obj;
    auto& fade = *obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    fade.BeginOut(0.0f);
    EXPECT_FALSE(fade.IsFading());
    EXPECT_TRUE(fade.IsBlack());

    fade.BeginIn(0.0f);
    EXPECT_FALSE(fade.IsFading());
    EXPECT_FALSE(fade.IsBlack());
    EXPECT_NEAR(fade.Alpha(), 0.0f, 1e-6f);
}

// 重ね描きの口を持つのは専用基底の派生だけ。 本番の呼び出しと同じリフレクション照合で確かめる
TEST(ScreenFade, OnlyOverlayRendererIsPickedUp)
{
    NS::Object::GameObject obj;
    auto* fade = obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    auto* transform = obj.FindComponent<NS::Object::TransformComponent>();
    ASSERT_NE(transform, nullptr);

    EXPECT_NE(
        NS::Object::ComponentCast<NS::Object::OverlayRendererComponent>(static_cast<NS::Object::Component*>(fade)),
        nullptr);
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::OverlayRendererComponent>(
                  static_cast<NS::Object::Component*>(transform)),
              nullptr);

    // 中間基底を挟んでも宣言する帯は変わらない
    EXPECT_EQ(fade->Priority(), NS::Object::TickPriority::LateUpdate + 5);
}

// 暗転は player に積む配置物側の component なので、 一時オブジェクトに絞ると一度も描かれない
TEST(ScreenFade, PlacedObjectFadeIsPickedUp)
{
    NS::Object::World world;
    auto* obj = world.Spawn<NS::Object::GameObject>();
    obj->AddComponent<NS::Game::Level::ScreenFadeComponent>();
    ASSERT_FALSE(obj->IsTransient());

    int found = 0;
    world.ForEachComponent<NS::Object::OverlayRendererComponent>(
        [&found](const NS::Object::OverlayRendererComponent&) { ++found; });
    EXPECT_EQ(found, 1);
}

// active を切った暗転は描かない。 更新・ 当たりと同じ問いで揃える
TEST(ScreenFade, InactiveFadeIsSkipped)
{
    NS::Object::GameObject obj;
    auto* fade = obj.AddComponent<NS::Game::Level::ScreenFadeComponent>();
    fade->BeginOut(0.0f);
    ASSERT_TRUE(fade->IsBlack());

    fade->SetActive(false);
    EXPECT_FALSE(fade->IsActive());
}
