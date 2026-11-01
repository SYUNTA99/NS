#include "NSlib/Gfx/Render/ScreenOverlay.h"

#include <gtest/gtest.h>

#include <cmath>

// 画面の斜めの線の四隅を、ピクセルの両端と太さからクリップ空間へ作る計算を縛る

namespace
{
    constexpr float k_Tolerance = 1.0e-5f;

    // クリップ空間の点をピクセルへ戻す。左上が原点で、y は下向き
    NS::Vector2 ToPixels(float clipX, float clipY, NS::Size2D target)
    {
        return NS::Vector2{(clipX + 1.0f) * 0.5f * static_cast<float>(target.width),
                           (1.0f - clipY) * 0.5f * static_cast<float>(target.height)};
    }
} // namespace

TEST(ScreenLineTest, HorizontalLineSpreadsHalfTheThicknessAboveAndBelow)
{
    const NS::Size2D target{200, 100};
    NS::Gfx::ScreenLineCB line{};
    ASSERT_TRUE(NS::Gfx::MakeScreenLineConstants(20.0f, 50.0f, 120.0f, 50.0f, 10.0f, NS::Color{1, 0, 0, 1}, target, line));
    // 四隅は 始まりの片側・始まりの反対側・終わりの片側・終わりの反対側 の順
    const NS::Vector2 c0 = ToPixels(line.cornersA[0], line.cornersA[1], target);
    const NS::Vector2 c1 = ToPixels(line.cornersA[2], line.cornersA[3], target);
    const NS::Vector2 c2 = ToPixels(line.cornersB[0], line.cornersB[1], target);
    const NS::Vector2 c3 = ToPixels(line.cornersB[2], line.cornersB[3], target);
    EXPECT_NEAR(c0.x, 20.0f, 1.0e-3f);
    EXPECT_NEAR(c1.x, 20.0f, 1.0e-3f);
    EXPECT_NEAR(c2.x, 120.0f, 1.0e-3f);
    EXPECT_NEAR(c3.x, 120.0f, 1.0e-3f);
    EXPECT_NEAR(std::abs(c0.y - c1.y), 10.0f, 1.0e-3f);
    EXPECT_NEAR((c0.y + c1.y) * 0.5f, 50.0f, 1.0e-3f);
    EXPECT_NEAR(std::abs(c2.y - c3.y), 10.0f, 1.0e-3f);
    EXPECT_NEAR(line.color.x, 1.0f, k_Tolerance);
}

TEST(ScreenLineTest, DiagonalLineKeepsTheThicknessAcrossTheLine)
{
    // 横長の画面でも、太さは線に垂直なピクセルの幅で揃う
    const NS::Size2D target{1280, 720};
    NS::Gfx::ScreenLineCB line{};
    ASSERT_TRUE(NS::Gfx::MakeScreenLineConstants(100.0f, 100.0f, 200.0f, 200.0f, 8.0f, NS::Color{}, target, line));
    const NS::Vector2 c0 = ToPixels(line.cornersA[0], line.cornersA[1], target);
    const NS::Vector2 c1 = ToPixels(line.cornersA[2], line.cornersA[3], target);
    EXPECT_NEAR((c0 - c1).Length(), 8.0f, 1.0e-2f);
    EXPECT_NEAR((c0 - c1).Dot(NS::Vector2{1.0f, 1.0f}), 0.0f, 1.0e-2f);
}

TEST(ScreenLineTest, ZeroLengthOrEmptyTargetIsRejectedWithoutWriting)
{
    NS::Gfx::ScreenLineCB line{};
    line.cornersA[0] = 7.0f;
    EXPECT_FALSE(NS::Gfx::MakeScreenLineConstants(5.0f, 5.0f, 5.0f, 5.0f, 4.0f, NS::Color{}, NS::Size2D{100, 100}, line));
    EXPECT_FALSE(NS::Gfx::MakeScreenLineConstants(0.0f, 0.0f, 9.0f, 0.0f, 4.0f, NS::Color{}, NS::Size2D{0, 100}, line));
    EXPECT_FALSE(NS::Gfx::MakeScreenLineConstants(0.0f, 0.0f, 9.0f, 0.0f, 0.0f, NS::Color{}, NS::Size2D{100, 100}, line));
    EXPECT_FLOAT_EQ(line.cornersA[0], 7.0f);
}
