#include "Editor/GizmoEditor.h"

#include "Framework/Scene/Transform.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{
    using NS::Editor::GizmoAxis;
    using NS::Editor::GizmoEditor;
    using NS::Editor::GizmoTool;

    TEST(GizmoEditor, DefaultToolIsSelect)
    {
        GizmoEditor gizmo;
        EXPECT_EQ(gizmo.Tool(), GizmoTool::Select);
    }

    TEST(GizmoEditor, NotActiveByDefault)
    {
        GizmoEditor gizmo;
        EXPECT_FALSE(gizmo.IsActive());
    }

    TEST(GizmoEditor, ToolForKeyQSelectsSelectTool)
    {
        EXPECT_EQ(GizmoEditor::ToolForKey(GizmoTool::Scale, NS::Platform::Key::Q), GizmoTool::Select);
    }

    TEST(GizmoEditor, ToolForKeyWSelectsMoveTool)
    {
        EXPECT_EQ(GizmoEditor::ToolForKey(GizmoTool::Select, NS::Platform::Key::W), GizmoTool::Move);
    }

    TEST(GizmoEditor, ToolForKeyESelectsRotateTool)
    {
        EXPECT_EQ(GizmoEditor::ToolForKey(GizmoTool::Select, NS::Platform::Key::E), GizmoTool::Rotate);
    }

    TEST(GizmoEditor, ToolForKeyRSelectsScaleTool)
    {
        EXPECT_EQ(GizmoEditor::ToolForKey(GizmoTool::Select, NS::Platform::Key::R), GizmoTool::Scale);
    }

    TEST(GizmoEditor, ToolForKeyNonToolKeyKeepsCurrent)
    {
        EXPECT_EQ(GizmoEditor::ToolForKey(GizmoTool::Rotate, NS::Platform::Key::A), GizmoTool::Rotate);
        EXPECT_EQ(GizmoEditor::ToolForKey(GizmoTool::Move, NS::Platform::Key::Space), GizmoTool::Move);
    }

    // -Z 向きの平行 ray を px の位置から撃つ。 X 軸ドラッグ平面 (z=startPos.z) との交点 x は px になる
    NS::Math::Ray MakeAxisProbeRayZ(float px, float startZ)
    {
        return NS::Math::Ray{NS::Math::Vector3{px, 0.0f, startZ + 5.0f}, NS::Math::Vector3{0.0f, 0.0f, -1.0f}};
    }

    TEST(GizmoEditorComputeAxisMove, NoneReturnsStart)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        const auto r0 = MakeAxisProbeRayZ(0.0f, start.z);
        const auto r1 = MakeAxisProbeRayZ(1.0f, start.z);
        const auto out = GizmoEditor::ComputeAxisMove(start, GizmoAxis::None, r0, r1, false);
        EXPECT_NEAR(out.x, start.x, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, UniformReturnsStart)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        const auto r0 = MakeAxisProbeRayZ(0.0f, start.z);
        const auto r1 = MakeAxisProbeRayZ(1.0f, start.z);
        const auto out = GizmoEditor::ComputeAxisMove(start, GizmoAxis::Uniform, r0, r1, false);
        EXPECT_NEAR(out.x, start.x, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, XAxisMovesOnlyX)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        const auto r0 = MakeAxisProbeRayZ(0.0f, start.z);
        const auto r1 = MakeAxisProbeRayZ(1.3f, start.z);
        const auto out = GizmoEditor::ComputeAxisMove(start, GizmoAxis::X, r0, r1, false);
        EXPECT_NEAR(out.x, start.x + 1.3f, 1e-3f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, XAxisParallelViewIsNoOp)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        // 視線方向が X 軸とほぼ平行 -> 縮退で no-op
        const NS::Math::Ray r0{NS::Math::Vector3{-5.0f, 3.0f, 4.0f}, NS::Math::Vector3{1.0f, 0.0f, 0.0f}};
        const NS::Math::Ray r1{NS::Math::Vector3{-5.0f, 3.0f, 4.0f}, NS::Math::Vector3{1.0f, 0.0f, 0.0f}};
        const auto out = GizmoEditor::ComputeAxisMove(start, GizmoAxis::X, r0, r1, false);
        EXPECT_NEAR(out.x, start.x, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, SnapRoundsToHalf)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        // delta=1.3 -> newX=3.3 -> snap(0.5) -> 3.5
        const auto r0 = MakeAxisProbeRayZ(0.0f, start.z);
        const auto r1 = MakeAxisProbeRayZ(1.3f, start.z);
        const auto out = GizmoEditor::ComputeAxisMove(start, GizmoAxis::X, r0, r1, true);
        EXPECT_NEAR(out.x, 3.5f, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    constexpr float kPi = 3.14159265358979323846f;

    // クォータニオン q が既知ベクトルに与える回転を成分比較する (quat 直接比較は ±符号曖昧を含むため避ける)
    void ExpectRotatesSame(const NS::Math::Quaternion& a, const NS::Math::Quaternion& b, float tol)
    {
        const NS::Math::Vector3 probes[3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        for (const auto& p : probes)
        {
            const auto ra = NS::Math::Vector3::Transform(p, a);
            const auto rb = NS::Math::Vector3::Transform(p, b);
            EXPECT_NEAR(ra.x, rb.x, tol);
            EXPECT_NEAR(ra.y, rb.y, tol);
            EXPECT_NEAR(ra.z, rb.z, tol);
        }
    }

    TEST(GizmoEditorScreenDragToAngle, QuarterTurnIsHalfPi)
    {
        // origin 中心、 +X 方向 (1,0) から +Y 方向 (0,1) へ 90 度回す
        const NS::Math::Vector2 origin{100.0f, 100.0f};
        const NS::Math::Vector2 start{200.0f, 100.0f};
        const NS::Math::Vector2 now{100.0f, 200.0f};
        EXPECT_NEAR(GizmoEditor::ScreenDragToAngle(origin, start, now), kPi / 2.0f, 1e-4f);
    }

    TEST(GizmoEditorScreenDragToAngle, ClockwiseIsNegative)
    {
        const NS::Math::Vector2 origin{0.0f, 0.0f};
        const NS::Math::Vector2 start{1.0f, 0.0f};
        const NS::Math::Vector2 now{0.0f, -1.0f};
        EXPECT_NEAR(GizmoEditor::ScreenDragToAngle(origin, start, now), -kPi / 2.0f, 1e-4f);
    }

    TEST(GizmoEditorScreenDragToAngle, NoMovementIsZero)
    {
        const NS::Math::Vector2 origin{5.0f, 5.0f};
        const NS::Math::Vector2 start{10.0f, 5.0f};
        EXPECT_NEAR(GizmoEditor::ScreenDragToAngle(origin, start, start), 0.0f, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisRotate, NoneReturnsStart)
    {
        const auto startRot = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, 0.7f);
        const auto out = GizmoEditor::ComputeAxisRotate(startRot, GizmoAxis::None, 0.5f, false);
        ExpectRotatesSame(out, startRot, 1e-5f);
    }

    TEST(GizmoEditorComputeAxisRotate, UniformReturnsStart)
    {
        const auto startRot = NS::Math::Quaternion::CreateFromAxisAngle({1.0f, 0.0f, 0.0f}, 0.3f);
        const auto out = GizmoEditor::ComputeAxisRotate(startRot, GizmoAxis::Uniform, 0.5f, false);
        ExpectRotatesSame(out, startRot, 1e-5f);
    }

    TEST(GizmoEditorComputeAxisRotate, IdentityYMatchesCreateFromAxisAngle)
    {
        const NS::Math::Quaternion identity = NS::Math::Quaternion::Identity;
        const float theta = 0.6f;
        const auto out = GizmoEditor::ComputeAxisRotate(identity, GizmoAxis::Y, theta, false);
        const auto expected = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, theta);
        ExpectRotatesSame(out, expected, 1e-5f);
    }

    TEST(GizmoEditorComputeAxisRotate, IdentityXMatchesCreateFromAxisAngle)
    {
        const NS::Math::Quaternion identity = NS::Math::Quaternion::Identity;
        const float theta = -0.9f;
        const auto out = GizmoEditor::ComputeAxisRotate(identity, GizmoAxis::X, theta, false);
        const auto expected = NS::Math::Quaternion::CreateFromAxisAngle({1.0f, 0.0f, 0.0f}, theta);
        ExpectRotatesSame(out, expected, 1e-5f);
    }

    TEST(GizmoEditorComputeAxisRotate, SnapsTo15Degrees)
    {
        const NS::Math::Quaternion identity = NS::Math::Quaternion::Identity;
        // 20 度相当 -> 最近接 15 度刻みは 15 度
        const float twentyDeg = kPi / 9.0f;
        const float fifteenDeg = kPi / 12.0f;
        const auto out = GizmoEditor::ComputeAxisRotate(identity, GizmoAxis::Z, twentyDeg, true);
        const auto expected = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 0.0f, 1.0f}, fifteenDeg);
        ExpectRotatesSame(out, expected, 1e-5f);
    }

    // 非単位の startRot に対して、 結果と startRot の「ワールド差分」 回転が要求軸 (±Y) になることを固定する
    // diff = conj(startRot) * out が +Y 軸の回転になっていれば合成順 startRot*delta が正しい
    TEST(GizmoEditorComputeAxisRotate, WorldAxisCompositionOrder)
    {
        const auto startRot = NS::Math::Quaternion::CreateFromAxisAngle({1.0f, 0.0f, 0.0f}, 0.5f);
        const float theta = kPi / 2.0f;
        const auto out = GizmoEditor::ComputeAxisRotate(startRot, GizmoAxis::Y, theta, false);

        NS::Math::Quaternion startConj = startRot;
        startConj.Conjugate();
        const NS::Math::Quaternion diff = startConj * out;
        const auto expectedDelta = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, theta);
        ExpectRotatesSame(diff, expectedDelta, 1e-4f);
    }

    TEST(GizmoEditor, PickNearestObbPicksNearerOfTwoAxisAlignedBoxes)
    {
        using NS::Math::Matrix;
        using NS::Math::Ray;
        using NS::Math::Vector3;

        // 2 つの単位箱を z=5 (近) と z=15 (遠) に置き、 +Z 向き ray を撃つ
        const std::array<Matrix, 2> worlds = {
            Matrix::CreateTranslation(0.0f, 0.0f, 5.0f),
            Matrix::CreateTranslation(0.0f, 0.0f, 15.0f),
        };
        const std::array<Vector3, 2> halfExtents = {
            Vector3{1.0f, 1.0f, 1.0f},
            Vector3{1.0f, 1.0f, 1.0f},
        };

        const Ray ray(Vector3{0.0f, 0.0f, -10.0f}, Vector3{0.0f, 0.0f, 1.0f});
        const int picked = GizmoEditor::PickNearestObb(ray, worlds, halfExtents);
        EXPECT_EQ(picked, 0); // 手前 (z=5) の box を選ぶ
    }

    TEST(GizmoEditor, PickNearestObbReturnsMinusOneWhenRayMisses)
    {
        using NS::Math::Matrix;
        using NS::Math::Ray;
        using NS::Math::Vector3;

        const std::array<Matrix, 1> worlds = {Matrix::CreateTranslation(0.0f, 0.0f, 5.0f)};
        const std::array<Vector3, 1> halfExtents = {Vector3{1.0f, 1.0f, 1.0f}};

        // box は原点周辺 (x in [-1,1])。 x=100 を通る +Z ray は完全に外す
        const Ray ray(Vector3{100.0f, 0.0f, -10.0f}, Vector3{0.0f, 0.0f, 1.0f});
        EXPECT_EQ(GizmoEditor::PickNearestObb(ray, worlds, halfExtents), -1);
    }

    TEST(GizmoEditor, PickNearestObbHitsRotatedBoxThatAxisAlignedWouldMiss)
    {
        using NS::Math::Matrix;
        using NS::Math::Ray;
        using NS::Math::Vector3;

        // box1: ローカル X に長い薄箱を Y 軸 45° 回転。 box0 はぶつからない遠方ダミー
        const std::array<Matrix, 2> worlds = {
            Matrix::CreateTranslation(0.0f, 50.0f, 0.0f),   // 遠方 (ヒットしない)
            Matrix::CreateRotationY(NS::Math::kPi * 0.25f), // 原点で 45° 回転
        };
        const std::array<Vector3, 2> halfExtents = {
            Vector3{1.0f, 1.0f, 1.0f}, Vector3{3.0f, 1.0f, 1.0f}, // ローカル X に長い
        };

        // ローカル点 (2,0,0) は回転後ワールド (2cos45, 0, -2sin45) ≈ (1.414, 0, -1.414)
        // ここを通す鉛直下向き ray を撃つ
        constexpr float kSqrt2Half = 1.41421356f;
        const Ray ray(Vector3{kSqrt2Half, 10.0f, -kSqrt2Half}, Vector3{0.0f, -1.0f, 0.0f});

        // 軸平行 AABB {3,1,1} なら z=-1.414 が [-1,1] 外で外すが、 回転考慮なら box1 を拾う
        const int picked = GizmoEditor::PickNearestObb(ray, worlds, halfExtents);
        EXPECT_EQ(picked, 1);
    }

    TEST(GizmoEditor, PickNearestObbEmptySpanReturnsMinusOne)
    {
        using NS::Math::Ray;
        using NS::Math::Vector3;

        const Ray ray(Vector3{0.0f, 0.0f, -10.0f}, Vector3{0.0f, 0.0f, 1.0f});
        const std::span<const NS::Math::Matrix> emptyWorlds{};
        const std::span<const NS::Math::Vector3> emptyExtents{};
        EXPECT_EQ(GizmoEditor::PickNearestObb(ray, emptyWorlds, emptyExtents), -1);
    }

    TEST(GizmoEditor, ComputeScaleXAxisOnlyAffectsX)
    {
        const NS::Math::Vector3 start{1.0f, 1.0f, 1.0f};
        const NS::Math::Vector3 result = GizmoEditor::ComputeScale(start, GizmoAxis::X, 0.5f, false);
        EXPECT_NEAR(result.x, 1.5f, 1.0e-5f);
        EXPECT_NEAR(result.y, 1.0f, 1.0e-5f);
        EXPECT_NEAR(result.z, 1.0f, 1.0e-5f);
    }

    TEST(GizmoEditor, ComputeScaleUniformAffectsAllComponents)
    {
        const NS::Math::Vector3 start{1.0f, 2.0f, 3.0f};
        const NS::Math::Vector3 result = GizmoEditor::ComputeScale(start, GizmoAxis::Uniform, 0.5f, false);
        EXPECT_NEAR(result.x, 1.5f, 1.0e-5f);
        EXPECT_NEAR(result.y, 2.5f, 1.0e-5f);
        EXPECT_NEAR(result.z, 3.5f, 1.0e-5f);
    }

    TEST(GizmoEditor, ComputeScaleClampsToMinimumOnLargeNegative)
    {
        const NS::Math::Vector3 start{1.0f, 1.0f, 1.0f};
        const NS::Math::Vector3 result = GizmoEditor::ComputeScale(start, GizmoAxis::X, -10.0f, false);
        EXPECT_NEAR(result.x, 0.01f, 1.0e-5f);
        EXPECT_NEAR(result.y, 1.0f, 1.0e-5f);
        EXPECT_NEAR(result.z, 1.0f, 1.0e-5f);
    }

    TEST(GizmoEditor, ComputeScaleNoneReturnsStart)
    {
        const NS::Math::Vector3 start{1.3f, 2.7f, 0.4f};
        const NS::Math::Vector3 result = GizmoEditor::ComputeScale(start, GizmoAxis::None, 0.5f, true);
        EXPECT_NEAR(result.x, 1.3f, 1.0e-5f);
        EXPECT_NEAR(result.y, 2.7f, 1.0e-5f);
        EXPECT_NEAR(result.z, 0.4f, 1.0e-5f);
    }

    TEST(GizmoEditor, ComputeScaleSnapRoundsToQuarterStep)
    {
        // 1.0 + 0.6 = 1.6 -> 最近接 0.25 倍は 1.5
        const NS::Math::Vector3 start{1.0f, 1.0f, 1.0f};
        const NS::Math::Vector3 result = GizmoEditor::ComputeScale(start, GizmoAxis::Uniform, 0.6f, true);
        EXPECT_NEAR(result.x, 1.5f, 1.0e-5f);
        EXPECT_NEAR(result.y, 1.5f, 1.0e-5f);
        EXPECT_NEAR(result.z, 1.5f, 1.0e-5f);
    }

    TEST(GizmoEditor, ComputeScaleSnapToZeroRescuedByMinimum)
    {
        // 1.0 + (-0.9) = 0.1 -> SnapTo(0.1, 0.25)=0 -> 下限 0.01 へ救済
        const NS::Math::Vector3 start{1.0f, 1.0f, 1.0f};
        const NS::Math::Vector3 result = GizmoEditor::ComputeScale(start, GizmoAxis::X, -0.9f, true);
        EXPECT_NEAR(result.x, 0.01f, 1.0e-5f);
        EXPECT_NEAR(result.y, 1.0f, 1.0e-5f);
        EXPECT_NEAR(result.z, 1.0f, 1.0e-5f);
    }

    TEST(GizmoEditor, ScreenDragToScaleAmountProjectsOntoAxis)
    {
        // 軸 +X 方向に 100px 右ドラッグ -> 100 * 0.01 = 1.0
        const float amount =
            GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2{1.0f, 0.0f}, NS::Math::Vector2{100.0f, 0.0f});
        EXPECT_NEAR(amount, 1.0f, 1.0e-4f);
    }

    TEST(GizmoEditor, ScreenDragToScaleAmountUsesNormalizedAxis)
    {
        // axisDir2d は非正規化でも normalize される。 軸(3,4)|len5| に (3,4) ドラッグ -> 5 * 0.01 = 0.05
        const float amount =
            GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2{3.0f, 4.0f}, NS::Math::Vector2{3.0f, 4.0f});
        EXPECT_NEAR(amount, 0.05f, 1.0e-4f);
    }

    TEST(GizmoEditor, ScreenDragToScaleAmountPerpendicularDragIsZero)
    {
        // 軸 +X に対し縦ドラッグは射影 0
        const float amount =
            GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2{1.0f, 0.0f}, NS::Math::Vector2{0.0f, 80.0f});
        EXPECT_NEAR(amount, 0.0f, 1.0e-4f);
    }

    TEST(GizmoEditor, ScreenDragToScaleAmountFallbackOnDegenerateAxis)
    {
        // 軸が縮退 (ほぼ零) -> |drag| * 0.01 * sign(drag.x)。 (3,4) は len5、 x>0 で +0.05
        const float amount =
            GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2{0.0f, 0.0f}, NS::Math::Vector2{3.0f, 4.0f});
        EXPECT_NEAR(amount, 0.05f, 1.0e-4f);
    }

    TEST(GizmoEditor, ScreenDragToScaleAmountFallbackNegativeXShrinks)
    {
        // 縮退軸で drag.x<0 -> 縮小方向の負値。 (-6,8) は len10、 -0.1
        const float amount =
            GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2{0.0f, 0.0f}, NS::Math::Vector2{-6.0f, 8.0f});
        EXPECT_NEAR(amount, -0.1f, 1.0e-4f);
    }

    // 投影規約: 単位行列を vp に使うと worldH={x,y,z,1} がそのまま clip になる (行ベクトル規約)
    // viewport 800x600 で origin{0,0,0}->(400,300) 画面中心、 +X{1,0,0}->(800,300)、 +Y{0,1,0}->(400,0)
    TEST(GizmoEditor, ToolHandlePickSelectAlwaysNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Select, NS::Math::Vector2{600.0f, 302.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    TEST(GizmoEditor, ToolHandlePickHitsXAxis)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // origin2d=(400,300) -> Xend2d=(800,300) の水平線分の真上 (600,303)、 距離約 3px
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Move, NS::Math::Vector2{600.0f, 303.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::X);
    }

    TEST(GizmoEditor, ToolHandlePickHitsYAxis)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // origin2d=(400,300) -> Yend2d=(400,0) の垂直線分の真横 (402,150)、 距離約 2px
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Move, NS::Math::Vector2{402.0f, 150.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::Y);
    }

    TEST(GizmoEditor, ToolHandlePickFarReturnsNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // どの軸線・中心からも 12px 以上離れた点
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Move, NS::Math::Vector2{700.0f, 500.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    TEST(GizmoEditor, ToolHandlePickScaleCenterIsUniform)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // 画面中心 origin2d=(400,300) のすぐ近く。 Scale では中心 Uniform が軸より優先される
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Scale, NS::Math::Vector2{402.0f, 301.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::Uniform);
    }

    TEST(GizmoEditor, ToolHandlePickScaleAxisStillHitsWhenCenterFar)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // 中心(400,300)から十分離れ Uniform 閾値外、 だが X 軸線(600,303)には近い -> Scale でも X 軸が取れる
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Scale, NS::Math::Vector2{600.0f, 303.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::X);
    }

    TEST(GizmoEditor, ToolHandlePickInvalidViewportReturnsNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{0, 0};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Move, NS::Math::Vector2{400.0f, 300.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    // 回転は軸リングで掴む。 identity VP では Z リングが楕円(中心 400,300)に投影される
    TEST(GizmoEditor, ToolHandlePickRotateHitsZRing)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // t=45° の Z リング点は screen (682.8, 87.9)。 X/Y 軸線から十分離れ Z リングだけが近い
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Rotate, NS::Math::Vector2{683.0f, 88.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::Z);
    }

    TEST(GizmoEditor, ToolHandlePickRotateFarReturnsNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // 中心寄りでどの軸リングからも 12px 超なので掴めない
        const auto axis =
            GizmoEditor::ToolHandlePick(origin, GizmoTool::Rotate, NS::Math::Vector2{440.0f, 260.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    // identity VP + 100x100 viewport では原点(0,0,0)が画面中心(50,50)へ投影され、
    // screen x 差 Δpx の X 軸移動が world Δx = 2*Δpx/width に対応する
    TEST(GizmoEditorDrag, MoveDragChangesOnlyAxisAndPushesOneEdit)
    {
        NS::Scene::Transform t;
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{60.0f, 50.0f});

        EXPECT_NEAR(t.Position().x, 0.2f, 1e-4f);
        EXPECT_NEAR(t.Position().y, 0.0f, 1e-4f);
        EXPECT_NEAR(t.Position().z, 0.0f, 1e-4f);
    }

    TEST(GizmoEditorDrag, UndoRestoresBeforeRedoRestoresAfter)
    {
        NS::Scene::Transform t;
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{60.0f, 50.0f});
        EXPECT_NEAR(t.Position().x, 0.2f, 1e-4f);

        EXPECT_TRUE(gizmo.Undo());
        EXPECT_NEAR(t.Position().x, 0.0f, 1e-4f);

        EXPECT_TRUE(gizmo.Redo());
        EXPECT_NEAR(t.Position().x, 0.2f, 1e-4f);
    }

    TEST(GizmoEditorDrag, UndoRedoOnEmptyHistoryReturnsFalse)
    {
        GizmoEditor gizmo;
        EXPECT_FALSE(gizmo.Undo());
        EXPECT_FALSE(gizmo.Redo());
    }

    TEST(GizmoEditorDrag, NewEditClearsRedoBranch)
    {
        NS::Scene::Transform t;
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{60.0f, 50.0f});
        EXPECT_TRUE(gizmo.Undo());

        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{40.0f, 50.0f});
        EXPECT_NEAR(t.Position().x, -0.2f, 1e-4f);
        EXPECT_FALSE(gizmo.Redo());
        EXPECT_NEAR(t.Position().x, -0.2f, 1e-4f);
    }

    TEST(GizmoEditorDrag, NoOpDragPushesNothing)
    {
        NS::Scene::Transform t;
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{50.0f, 50.0f});
        EXPECT_FALSE(gizmo.Undo());
    }

    TEST(GizmoEditorDrag, NoSelectionDragIsNoOp)
    {
        GizmoEditor gizmo;
        gizmo.SetToolForTest(GizmoTool::Move);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{60.0f, 50.0f});
        EXPECT_FALSE(gizmo.Undo());
    }

    TEST(GizmoEditorDrag, RotateDragChangesRotationAndUndoRestores)
    {
        NS::Scene::Transform t;
        const NS::Math::Quaternion identity = t.Rotation();

        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Rotate);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{60.0f, 50.0f}, NS::Math::Vector2{50.0f, 60.0f});

        const NS::Math::Quaternion after = t.Rotation();
        const float dot = identity.x * after.x + identity.y * after.y + identity.z * after.z + identity.w * after.w;
        EXPECT_LT(std::fabs(dot), 0.9999f);

        EXPECT_TRUE(gizmo.Undo());
        EXPECT_NEAR(t.Rotation().x, identity.x, 1e-4f);
        EXPECT_NEAR(t.Rotation().y, identity.y, 1e-4f);
        EXPECT_NEAR(t.Rotation().z, identity.z, 1e-4f);
        EXPECT_NEAR(t.Rotation().w, identity.w, 1e-4f);
    }

    TEST(GizmoEditorDrag, UniformScaleDragGrowsAllAxesUniformly)
    {
        NS::Scene::Transform t;
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Scale);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::Uniform, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{70.0f, 50.0f});

        EXPECT_GT(t.Scale().x, 1.0f);
        EXPECT_NEAR(t.Scale().x, t.Scale().y, 1e-4f);
        EXPECT_NEAR(t.Scale().y, t.Scale().z, 1e-4f);

        EXPECT_TRUE(gizmo.Undo());
        EXPECT_NEAR(t.Scale().x, 1.0f, 1e-4f);
    }

    TEST(GizmoEditorTick, InactiveTickIsNoOp)
    {
        GizmoEditor gizmo;
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.Tick(vp, viewport);
        EXPECT_EQ(gizmo.Tool(), GizmoTool::Select);
        EXPECT_EQ(gizmo.Selected(), nullptr);
    }

    TEST(GizmoEditorTick, ActiveWithoutInputIsNoOp)
    {
        GizmoEditor gizmo;
        gizmo.SetActive(true);
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.Tick(vp, viewport);
        EXPECT_EQ(gizmo.Selected(), nullptr);
    }
} // namespace
