#include "NSlib/CoreObject/ClassObjs/WorldLines.h"

#include <gtest/gtest.h>

#include <vector>

// 世界の座標の線を前と今の 2 つで持ち、描く時の割合で混ぜて画素へ投げる計算を縛る

namespace
{
    const NS::Size2D k_Target{1280, 720};

    // 手前の (0, 1, -5) から奥の (0, 1, 0) を見るカメラ
    NS::Matrix ViewProjection()
    {
        const NS::Matrix view =
            NS::Matrix::CreateLookAt(NS::Vector3{0.0f, 1.0f, -5.0f}, NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector3::Up);
        return view * NS::Matrix::CreatePerspectiveFieldOfView(1.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
    }

    NS::WorldLine Line(const NS::Vector3& start, const NS::Vector3& end)
    {
        return NS::WorldLine{.start = start, .end = end, .thickness = 0.01f, .color = NS::Color{1.0f, 0.0f, 0.0f, 1.0f}};
    }
} // namespace

// 端の点は前と今の線を割合で混ぜる。割合 0.5 なら真ん中で画素へ投げる
TEST(WorldLines, EndsAreInterpolatedBetweenThePreviousAndTheCurrentLines)
{
    NS::WorldLines lines;
    lines.SetLines({Line(NS::Vector3{-1.0f, 1.0f, 0.0f}, NS::Vector3{-1.0f, 2.0f, 0.0f})});
    lines.Snapshot();
    lines.SetLines({Line(NS::Vector3{1.0f, 1.0f, 0.0f}, NS::Vector3{1.0f, 2.0f, 0.0f})});
    ASSERT_EQ(lines.Count(), 1u);

    const NS::WorldLine middle = lines.LineAt(0, 0.5f);
    EXPECT_NEAR(middle.start.x, 0.0f, 0.0001f);
    EXPECT_NEAR(middle.end.y, 2.0f, 0.0001f);

    NS::PixelLine pixels{};
    ASSERT_TRUE(NS::TryProjectWorldLine(middle, ViewProjection(), k_Target, pixels));
    EXPECT_NEAR(pixels.start.x, 640.0f, 0.5f);
    EXPECT_NEAR(pixels.start.y, 360.0f, 0.5f);
    EXPECT_LT(pixels.end.y, pixels.start.y) << "上へ伸びる線は画面の上へ伸びる";
    EXPECT_FLOAT_EQ(pixels.thickness, 0.01f * 720.0f);
}

// 前と今の線の数が違う歩は、別の線どうしを混ぜずに今の線を描く
TEST(WorldLines, ACountChangeDrawsTheCurrentLines)
{
    NS::WorldLines lines;
    lines.SetLines({});
    lines.Snapshot();
    lines.SetLines({Line(NS::Vector3{1.0f, 1.0f, 0.0f}, NS::Vector3{1.0f, 2.0f, 0.0f})});
    EXPECT_FLOAT_EQ(lines.LineAt(0, 0.0f).start.x, 1.0f);
}

// カメラの後ろに出る線と、画面での長さが太さより短い線 (カメラへまっすぐ向く物) は投げない
TEST(WorldLines, LinesBehindTheCameraOrPointingAtItAreNotProjected)
{
    NS::PixelLine pixels{};
    EXPECT_FALSE(NS::TryProjectWorldLine(
        Line(NS::Vector3{0.0f, 1.0f, -8.0f}, NS::Vector3{1.0f, 1.0f, -8.0f}), ViewProjection(), k_Target, pixels));
    EXPECT_FALSE(NS::TryProjectWorldLine(
        Line(NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector3{0.0f, 1.0f, -1.0f}), ViewProjection(), k_Target, pixels));
}
