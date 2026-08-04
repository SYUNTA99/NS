#include "Editor/GizmoEditor.h"
#include "Runtime/Object/Transform.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>

namespace
{
    using NS::Editor::GizmoAxis;
    using NS::Editor::GizmoEditor;
    using NS::Editor::GizmoSpace;
    using NS::Editor::GizmoTool;

    TEST(GizmoEditor, DefaultToolIsMove)
    {
        GizmoEditor gizmo;
        EXPECT_EQ(gizmo.Tool(), GizmoTool::Move);
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
        const auto out =
            GizmoEditor::ComputeAxisMove(start, GizmoAxis::None, NS::Math::Quaternion::Identity, r0, r1, false);
        EXPECT_NEAR(out.x, start.x, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, UniformReturnsStart)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        const auto r0 = MakeAxisProbeRayZ(0.0f, start.z);
        const auto r1 = MakeAxisProbeRayZ(1.0f, start.z);
        const auto out =
            GizmoEditor::ComputeAxisMove(start, GizmoAxis::Uniform, NS::Math::Quaternion::Identity, r0, r1, false);
        EXPECT_NEAR(out.x, start.x, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, XAxisMovesOnlyX)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        const auto r0 = MakeAxisProbeRayZ(0.0f, start.z);
        const auto r1 = MakeAxisProbeRayZ(1.3f, start.z);
        const auto out =
            GizmoEditor::ComputeAxisMove(start, GizmoAxis::X, NS::Math::Quaternion::Identity, r0, r1, false);
        EXPECT_NEAR(out.x, start.x + 1.3f, 1e-3f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    TEST(GizmoEditorComputeAxisMove, XAxisParallelViewIsNoOp)
    {
        const NS::Math::Vector3 start{2.0f, 3.0f, 4.0f};
        // 視線方向が X 軸とほぼ平行 -> 縮退時は何もしない
        const NS::Math::Ray r0{NS::Math::Vector3{-5.0f, 3.0f, 4.0f}, NS::Math::Vector3{1.0f, 0.0f, 0.0f}};
        const NS::Math::Ray r1{NS::Math::Vector3{-5.0f, 3.0f, 4.0f}, NS::Math::Vector3{1.0f, 0.0f, 0.0f}};
        const auto out =
            GizmoEditor::ComputeAxisMove(start, GizmoAxis::X, NS::Math::Quaternion::Identity, r0, r1, false);
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
        const auto out =
            GizmoEditor::ComputeAxisMove(start, GizmoAxis::X, NS::Math::Quaternion::Identity, r0, r1, true);
        EXPECT_NEAR(out.x, 3.5f, 1e-4f);
        EXPECT_NEAR(out.y, start.y, 1e-4f);
        EXPECT_NEAR(out.z, start.z, 1e-4f);
    }

    constexpr float k_Pi = 3.14159265358979323846f;

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

    // 単位 VP は world {x,y,z,1} がそのまま clip + 視線 +Z。 viewport 800x600 では
    // screen(600,300)->world(0.5,0,0)、 screen(400,150)->world(0,0.5,0) に逆投影される
    TEST(GizmoEditorWorldDragToAngle, GrabXDragToYAroundZIsQuarterTurn)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const float angle = GizmoEditor::WorldDragToAngle(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                                                          GizmoAxis::Z,
                                                          NS::Math::Quaternion::Identity,
                                                          vp,
                                                          viewport,
                                                          NS::Math::Vector2{600.0f, 300.0f},
                                                          NS::Math::Vector2{400.0f, 150.0f});
        EXPECT_NEAR(angle, k_Pi / 2.0f, 1e-3f);
    }

    // 掴み点と運び先を入れ替えると逆回り
    TEST(GizmoEditorWorldDragToAngle, ReverseDragNegatesAngle)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const float angle = GizmoEditor::WorldDragToAngle(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                                                          GizmoAxis::Z,
                                                          NS::Math::Quaternion::Identity,
                                                          vp,
                                                          viewport,
                                                          NS::Math::Vector2{400.0f, 150.0f},
                                                          NS::Math::Vector2{600.0f, 300.0f});
        EXPECT_NEAR(angle, -k_Pi / 2.0f, 1e-3f);
    }

    TEST(GizmoEditorWorldDragToAngle, NonAxisReturnsZero)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        EXPECT_NEAR(GizmoEditor::WorldDragToAngle(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                                                  GizmoAxis::None,
                                                  NS::Math::Quaternion::Identity,
                                                  vp,
                                                  viewport,
                                                  NS::Math::Vector2{600.0f, 300.0f},
                                                  NS::Math::Vector2{400.0f, 150.0f}),
                    0.0f,
                    1e-6f);
        EXPECT_NEAR(GizmoEditor::WorldDragToAngle(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                                                  GizmoAxis::Uniform,
                                                  NS::Math::Quaternion::Identity,
                                                  vp,
                                                  viewport,
                                                  NS::Math::Vector2{600.0f, 300.0f},
                                                  NS::Math::Vector2{400.0f, 150.0f}),
                    0.0f,
                    1e-6f);
    }

    // 単位 VP の視線は +Z。 X 軸リング平面 (YZ) を真横から見るため交点が定まらず 0 (縮退)
    TEST(GizmoEditorWorldDragToAngle, EdgeOnPlaneReturnsZero)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        EXPECT_NEAR(GizmoEditor::WorldDragToAngle(NS::Math::Vector3{0.0f, 0.0f, 0.0f},
                                                  GizmoAxis::X,
                                                  NS::Math::Quaternion::Identity,
                                                  vp,
                                                  viewport,
                                                  NS::Math::Vector2{600.0f, 300.0f},
                                                  NS::Math::Vector2{400.0f, 150.0f}),
                    0.0f,
                    1e-6f);
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
        const float twentyDeg = k_Pi / 9.0f;
        const float fifteenDeg = k_Pi / 12.0f;
        const auto out = GizmoEditor::ComputeAxisRotate(identity, GizmoAxis::Z, twentyDeg, true);
        const auto expected = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 0.0f, 1.0f}, fifteenDeg);
        ExpectRotatesSame(out, expected, 1e-5f);
    }

    // local 軸回転の担保。 選択物を local Y 周りに回しても、 その local Y (world 像) は不動のまま (world Y
    // 固定の旧挙動なら startRot が非単位のとき local Y は動く)。 これで local 化を弁別する
    TEST(GizmoEditorComputeAxisRotate, LocalRotateLeavesLocalAxisFixed)
    {
        const auto startRot = NS::Math::Quaternion::CreateFromAxisAngle({1.0f, 0.0f, 0.0f}, 0.5f);
        const float theta = k_Pi / 2.0f;
        const auto out = GizmoEditor::ComputeAxisRotate(startRot, GizmoAxis::Y, theta, false);

        const auto localYBefore = NS::Math::Vector3::Transform({0.0f, 1.0f, 0.0f}, startRot);
        const auto localYAfter = NS::Math::Vector3::Transform({0.0f, 1.0f, 0.0f}, out);
        EXPECT_NEAR(localYAfter.x, localYBefore.x, 1e-4f);
        EXPECT_NEAR(localYAfter.y, localYBefore.y, 1e-4f);
        EXPECT_NEAR(localYAfter.z, localYBefore.z, 1e-4f);
    }

    // 世界差分回転 conj(startRot)*out が local 軸 (startRot で回した Y) 周りになることを固定する
    // 旧 world 挙動なら差分は world Y 周りなので、 両者を弁別する
    TEST(GizmoEditorComputeAxisRotate, LocalAxisCompositionOrder)
    {
        const auto startRot = NS::Math::Quaternion::CreateFromAxisAngle({1.0f, 0.0f, 0.0f}, 0.5f);
        const float theta = k_Pi / 2.0f;
        const auto out = GizmoEditor::ComputeAxisRotate(startRot, GizmoAxis::Y, theta, false);

        NS::Math::Quaternion startConj = startRot;
        startConj.Conjugate();
        const NS::Math::Quaternion diff = startConj * out;
        const auto localAxis = NS::Math::Vector3::Transform({0.0f, 1.0f, 0.0f}, startRot);
        const auto expectedDelta = NS::Math::Quaternion::CreateFromAxisAngle(localAxis, theta);
        ExpectRotatesSame(diff, expectedDelta, 1e-4f);
    }

    // World 空間回転は world 軸で回す。 非単位 startRot でも世界差分が world Y 周りになる (local 化の逆)
    TEST(GizmoEditorComputeAxisRotate, WorldRotateUsesWorldAxis)
    {
        const auto startRot = NS::Math::Quaternion::CreateFromAxisAngle({1.0f, 0.0f, 0.0f}, 0.5f);
        const float theta = k_Pi / 2.0f;
        const auto out = GizmoEditor::ComputeAxisRotate(startRot, GizmoAxis::Y, theta, false, /*worldSpace=*/true);

        NS::Math::Quaternion startConj = startRot;
        startConj.Conjugate();
        const NS::Math::Quaternion diff = startConj * out;
        const auto expected = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, theta);
        ExpectRotatesSame(diff, expected, 1e-4f);
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

    TEST(GizmoEditor, PickNearestObbMaskSkipsNearerBoxSoFartherWins)
    {
        using NS::Math::Matrix;
        using NS::Math::Ray;
        using NS::Math::Vector3;

        // 手前 z=5 と奥 z=15 の箱。 mask で手前を対象外にすると、 手前が最近でも奥を拾う
        // 見えないカメラが重なった可視ブロックの pick を取らない 2 パス目相当の検証
        const std::array<Matrix, 2> worlds = {
            Matrix::CreateTranslation(0.0f, 0.0f, 5.0f),
            Matrix::CreateTranslation(0.0f, 0.0f, 15.0f),
        };
        const std::array<Vector3, 2> halfExtents = {
            Vector3{1.0f, 1.0f, 1.0f},
            Vector3{1.0f, 1.0f, 1.0f},
        };
        const std::array<std::uint8_t, 2> mask = {0, 1};

        const Ray ray(Vector3{0.0f, 0.0f, -10.0f}, Vector3{0.0f, 0.0f, 1.0f});
        EXPECT_EQ(GizmoEditor::PickNearestObb(ray, worlds, halfExtents, mask), 1);
        // mask 無し (空 span) なら従来通り手前を拾う
        EXPECT_EQ(GizmoEditor::PickNearestObb(ray, worlds, halfExtents), 0);
    }

    TEST(GizmoEditor, PickNearestObbReturnsMinusOneWhenMaskExcludesAllHits)
    {
        using NS::Math::Matrix;
        using NS::Math::Ray;
        using NS::Math::Vector3;

        // 全候補を mask=0 にすると、 ray が当たっても無ヒット扱い。 controller はこの後 2 パス目で全体を撃つ
        const std::array<Matrix, 1> worlds = {Matrix::CreateTranslation(0.0f, 0.0f, 5.0f)};
        const std::array<Vector3, 1> halfExtents = {Vector3{1.0f, 1.0f, 1.0f}};
        const std::array<std::uint8_t, 1> mask = {0};

        const Ray ray(Vector3{0.0f, 0.0f, -10.0f}, Vector3{0.0f, 0.0f, 1.0f});
        EXPECT_EQ(GizmoEditor::PickNearestObb(ray, worlds, halfExtents, mask), -1);
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
            Matrix::CreateTranslation(0.0f, 50.0f, 0.0f),    // 遠方 (ヒットしない)
            Matrix::CreateRotationY(NS::Math::k_Pi * 0.25f), // 原点で 45° 回転
        };
        const std::array<Vector3, 2> halfExtents = {
            Vector3{1.0f, 1.0f, 1.0f}, Vector3{3.0f, 1.0f, 1.0f}, // ローカル X に長い
        };

        // ローカル点 (2,0,0) は回転後ワールド (2cos45, 0, -2sin45) ≈ (1.414, 0, -1.414)
        // ここを通す鉛直下向き ray を撃つ
        constexpr float k_Sqrt2Half = 1.41421356f;
        const Ray ray(Vector3{k_Sqrt2Half, 10.0f, -k_Sqrt2Half}, Vector3{0.0f, -1.0f, 0.0f});

        // 軸平行 AABB {3,1,1} なら z=-1.414 が [-1,1] 外で外すが、 回転考慮なら box1 を拾う
        const int picked = GizmoEditor::PickNearestObb(ray, worlds, halfExtents);
        EXPECT_EQ(picked, 1);
    }

    // スケール box を掴む回帰。 逆ワールド行列にスケール逆数が入り localDir が非単位になる経路で、
    // 単位ベクトル assert を踏まず正しく拾えること (スケール軸に沿う ray でその条件を作る)
    TEST(GizmoEditor, PickNearestObbHitsScaledBoxAlongScaledAxis)
    {
        using NS::Math::Matrix;
        using NS::Math::Ray;
        using NS::Math::Vector3;

        const std::array<Matrix, 1> worlds = {Matrix::CreateScale(4.0f, 1.0f, 1.0f)};
        const std::array<Vector3, 1> halfExtents = {Vector3{1.0f, 1.0f, 1.0f}};

        // world box は x in [-4,4]。 x=-10 から +X 撃つと x=-4 で当たる
        const Ray ray(Vector3{-10.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f});
        EXPECT_EQ(GizmoEditor::PickNearestObb(ray, worlds, halfExtents), 0);
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
        // 1.0 + (-0.9) = 0.1 -> SnapTo(0.1, 0.25)=0 -> 下限 0.01 へクランプ
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
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Select, NS::Math::Vector2{600.0f, 302.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    TEST(GizmoEditor, ToolHandlePickHitsXAxis)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // origin2d=(400,300) -> Xend2d=(800,300) の水平線分の真上 (600,303)、 距離約 3px
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Move, NS::Math::Vector2{600.0f, 303.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::X);
    }

    TEST(GizmoEditor, ToolHandlePickHitsYAxis)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // origin2d=(400,300) -> Yend2d=(400,0) の垂直線分の真横 (402,150)、 距離約 2px
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Move, NS::Math::Vector2{402.0f, 150.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::Y);
    }

    TEST(GizmoEditor, ToolHandlePickFarReturnsNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // どの軸線・中心からも 12px 以上離れた点
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Move, NS::Math::Vector2{700.0f, 500.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    TEST(GizmoEditor, ToolHandlePickScaleCenterIsUniform)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // 画面中心 origin2d=(400,300) のすぐ近く。 Scale では中心 Uniform が軸より優先される
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Scale, NS::Math::Vector2{402.0f, 301.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::Uniform);
    }

    TEST(GizmoEditor, ToolHandlePickScaleAxisStillHitsWhenCenterFar)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // 中心(400,300)から十分離れ Uniform 閾値外、 だが X 軸線(600,303)には近い -> Scale でも X 軸が取れる
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Scale, NS::Math::Vector2{600.0f, 303.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::X);
    }

    TEST(GizmoEditor, ToolHandlePickInvalidViewportReturnsNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{0, 0};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Move, NS::Math::Vector2{400.0f, 300.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    // 回転は軸リングで掴む。 identity VP では Z リングが楕円(中心 400,300)に投影される
    TEST(GizmoEditor, ToolHandlePickRotateHitsZRing)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // t=45° の Z リング点は screen (682.8, 87.9)。 X/Y 軸線から十分離れ Z リングだけが近い
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Rotate, NS::Math::Vector2{683.0f, 88.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::Z);
    }

    TEST(GizmoEditor, ToolHandlePickRotateFarReturnsNone)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        // 中心寄りでどの軸リングからも 12px 超なので掴めない
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, NS::Math::Quaternion::Identity, GizmoTool::Rotate, NS::Math::Vector2{440.0f, 260.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::None);
    }

    // Z 90° 回転で local X ハンドルは画面上の world Y 位置を向く。 旧 world 固定なら Y が取れた所で X が取れる
    TEST(GizmoEditor, ToolHandlePickFollowsLocalAxisWhenRotated)
    {
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{800, 600};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const auto rotation = NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 0.0f, 1.0f}, k_Pi / 2.0f);
        // (402,150) は identity なら world Y 軸線上だが、 local X がそこを向くので X になる
        const auto axis = GizmoEditor::ToolHandlePick(
            origin, rotation, GizmoTool::Move, NS::Math::Vector2{402.0f, 150.0f}, vp, viewport);
        EXPECT_EQ(axis, GizmoAxis::X);
    }

    // identity VP + 100x100 viewport では原点(0,0,0)が画面中心(50,50)へ投影され、
    // screen x 差 Δpx の X 軸移動が world Δx = 2*Δpx/width に対応する
    TEST(GizmoEditorDrag, MoveDragChangesOnlyAxis)
    {
        NS::Object::Transform t;
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

    // 選択物を Z 90° 回した状態で local X をドラッグすると、 world では X でなく Y 方向へ動く
    TEST(GizmoEditorDrag, MoveFollowsLocalAxisWhenRotated)
    {
        NS::Object::Transform t;
        t.SetRotation(NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 0.0f, 1.0f}, k_Pi / 2.0f));
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);

        // local X は world +Y を向く。 軸が画面縦に投影されるので縦ドラッグで動かす
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{50.0f, 40.0f});

        EXPECT_NEAR(t.Position().x, 0.0f, 1e-3f);
        EXPECT_NEAR(t.Position().z, 0.0f, 1e-3f);
        EXPECT_GT(std::fabs(t.Position().y), 0.05f);
    }

    // World 空間では Z 90° 回しても local X ハンドル = world X。 横ドラッグで world X が動く (local 化の逆)
    TEST(GizmoEditorDrag, MoveUsesWorldAxisInWorldSpace)
    {
        NS::Object::Transform t;
        t.SetRotation(NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 0.0f, 1.0f}, k_Pi / 2.0f));
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);
        gizmo.SetSpace(GizmoSpace::World);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{60.0f, 50.0f});

        EXPECT_NEAR(t.Position().x, 0.2f, 1e-3f);
        EXPECT_NEAR(t.Position().y, 0.0f, 1e-3f);
    }

    TEST(GizmoEditorDrag, NoOpDragLeavesPositionUnchanged)
    {
        NS::Object::Transform t;
        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Move);

        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::X, NS::Math::Vector2{50.0f, 50.0f}, NS::Math::Vector2{50.0f, 50.0f});
        EXPECT_NEAR(t.Position().x, 0.0f, 1e-4f);
    }

    TEST(GizmoEditorDrag, RotateDragChangesRotation)
    {
        NS::Object::Transform t;
        const NS::Math::Quaternion identity = t.Rotation();

        GizmoEditor gizmo;
        gizmo.SelectForTest(&t);
        gizmo.SetToolForTest(GizmoTool::Rotate);

        // 単位 VP の視線は +Z。 Z 軸リング平面 (XY) はカメラ正面なので回転が定まる
        const NS::Math::Matrix vp;
        const NS::Math::Size2D viewport{100, 100};
        gizmo.ApplyDragForTest(
            vp, viewport, GizmoAxis::Z, NS::Math::Vector2{60.0f, 50.0f}, NS::Math::Vector2{50.0f, 60.0f});

        const NS::Math::Quaternion after = t.Rotation();
        const float dot = identity.x * after.x + identity.y * after.y + identity.z * after.z + identity.w * after.w;
        EXPECT_LT(std::fabs(dot), 0.9999f);
    }

    // ワールドの同じ点を掴んで同じワールド点へ運ぶドラッグは、 カメラが軸の表から見ても裏から
    // 見ても同じワールド回転になるべき。 screen 2D 角だけで決めると裏視点で逆回転する
    TEST(GizmoEditorDrag, RotateSameWorldRotationFromOppositeCameraSides)
    {
        const NS::Math::Size2D viewport{800, 600};
        const float aspect = 800.0f / 600.0f;
        const auto proj = NS::Math::Matrix::CreatePerspectiveFieldOfView(k_Pi / 3.0f, aspect, 0.1f, 100.0f);
        const auto viewFront =
            NS::Math::Matrix::CreateLookAt({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
        const auto viewBack =
            NS::Math::Matrix::CreateLookAt({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
        const NS::Math::Matrix vpFront = viewFront * proj;
        const NS::Math::Matrix vpBack = viewBack * proj;

        auto projectToScreen =
            [](const NS::Math::Matrix& vp, NS::Math::Size2D vpSize, NS::Math::Vector3 w) -> NS::Math::Vector2 {
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(NS::Math::Vector4{w.x, w.y, w.z, 1.0f}, vp);
            NS::Math::Vector2 s{};
            s.x = ((clip.x / clip.w) * 0.5f + 0.5f) * static_cast<float>(vpSize.width);
            s.y = (1.0f - ((clip.y / clip.w) * 0.5f + 0.5f)) * static_cast<float>(vpSize.height);
            return s;
        };

        // Z 軸リングを「+X 点を掴んで +Y 点へ」 運ぶ。 これはどちらの視点でも同じワールド操作
        NS::Object::Transform tFront;
        GizmoEditor gFront;
        gFront.SelectForTest(&tFront);
        gFront.SetToolForTest(GizmoTool::Rotate);
        gFront.ApplyDragForTest(vpFront,
                                viewport,
                                GizmoAxis::Z,
                                projectToScreen(vpFront, viewport, {1.0f, 0.0f, 0.0f}),
                                projectToScreen(vpFront, viewport, {0.0f, 1.0f, 0.0f}));

        NS::Object::Transform tBack;
        GizmoEditor gBack;
        gBack.SelectForTest(&tBack);
        gBack.SetToolForTest(GizmoTool::Rotate);
        gBack.ApplyDragForTest(vpBack,
                               viewport,
                               GizmoAxis::Z,
                               projectToScreen(vpBack, viewport, {1.0f, 0.0f, 0.0f}),
                               projectToScreen(vpBack, viewport, {0.0f, 1.0f, 0.0f}));

        ExpectRotatesSame(tFront.Rotation(), tBack.Rotation(), 3e-2f);
    }

    TEST(GizmoEditorDrag, UniformScaleDragGrowsAllAxesUniformly)
    {
        NS::Object::Transform t;
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
    }

    TEST(GizmoEditorTick, InactiveTickIsNoOp)
    {
        GizmoEditor gizmo;
        const NS::Math::Matrix vp;
        const NS::Editor::ViewRect view{0, 0, 100, 100};
        gizmo.Tick(vp, view);
        EXPECT_EQ(gizmo.Tool(), GizmoTool::Move);
        EXPECT_EQ(gizmo.Selected(), nullptr);
    }

    TEST(GizmoEditorTick, ActiveWithoutInputIsNoOp)
    {
        GizmoEditor gizmo;
        gizmo.SetActive(true);
        const NS::Math::Matrix vp;
        const NS::Editor::ViewRect view{0, 0, 100, 100};
        gizmo.Tick(vp, view);
        EXPECT_EQ(gizmo.Selected(), nullptr);
    }

    constexpr int k_ViewWidth = 1280;
    constexpr int k_ViewHeight = 720;

    // 実機の編集視点に近い、 斜め見下ろしの view projection
    NS::Math::Matrix MakeOrbitViewProjection(const NS::Math::Vector3& eye, const NS::Math::Vector3& target)
    {
        NS::Math::Matrix view;
        DirectX::XMStoreFloat4x4(&view,
                                 DirectX::XMMatrixLookAtLH(DirectX::XMLoadFloat3(&eye),
                                                           DirectX::XMLoadFloat3(&target),
                                                           DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
        NS::Math::Matrix proj;
        const float aspect = static_cast<float>(k_ViewWidth) / static_cast<float>(k_ViewHeight);
        DirectX::XMStoreFloat4x4(&proj, DirectX::XMMatrixPerspectiveFovLH(60.0f * k_Pi / 180.0f, aspect, 0.1f, 500.0f));
        return view * proj;
    }

    // ScreenToWorldRay と同じ画面座標の取り方で world 点を落とす
    bool ProjectPoint(const NS::Math::Matrix& vp, const NS::Math::Vector3& world, NS::Math::Vector2& out)
    {
        const NS::Math::Vector4 clip =
            NS::Math::Vector4::Transform(NS::Math::Vector4{world.x, world.y, world.z, 1.0f}, vp);
        if (clip.w <= 1.0e-4f)
            return false;
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        out = NS::Math::Vector2{(ndcX * 0.5f + 0.5f) * static_cast<float>(k_ViewWidth),
                                (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(k_ViewHeight)};
        return true;
    }

    // 矢印の途中を掴んで画面上で引く。 掴めた軸と、 その軸に沿って動いた量を見る
    void ExpectArrowGrabAndDrag(GizmoAxis axis, const NS::Math::Vector3& axisDir)
    {
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const NS::Math::Matrix vp = MakeOrbitViewProjection(NS::Math::Vector3{6.0f, 5.0f, -7.0f}, origin);
        const NS::Math::Size2D viewport{k_ViewWidth, k_ViewHeight};

        NS::Math::Vector2 origin2d{};
        NS::Math::Vector2 grab2d{};
        ASSERT_TRUE(ProjectPoint(vp, origin, origin2d));
        ASSERT_TRUE(ProjectPoint(vp, origin + axisDir * 0.6f, grab2d));

        const GizmoAxis picked =
            GizmoEditor::ToolHandlePick(origin, NS::Math::Quaternion::Identity, GizmoTool::Move, grab2d, vp, viewport);
        EXPECT_EQ(picked, axis);

        // 掴んだ点から矢印の伸びる向きへ 60 px 引く
        NS::Math::Vector2 screenDir{grab2d.x - origin2d.x, grab2d.y - origin2d.y};
        const float length = std::sqrt(screenDir.x * screenDir.x + screenDir.y * screenDir.y);
        ASSERT_GT(length, 1.0f);
        screenDir.x = (screenDir.x / length) * 60.0f;
        screenDir.y = (screenDir.y / length) * 60.0f;

        const NS::Math::Ray rayStart =
            NS::Editor::ScreenToWorldRay(vp, viewport, static_cast<int>(grab2d.x), static_cast<int>(grab2d.y));
        const NS::Math::Ray rayNow = NS::Editor::ScreenToWorldRay(
            vp, viewport, static_cast<int>(grab2d.x + screenDir.x), static_cast<int>(grab2d.y + screenDir.y));

        const NS::Math::Vector3 moved =
            GizmoEditor::ComputeAxisMove(origin, axis, NS::Math::Quaternion::Identity, rayStart, rayNow, false);
        EXPECT_GT((moved - origin).Dot(axisDir), 0.1f);
    }

    // 点から線分までの画面上の距離
    float PixelDistanceToSegment(NS::Math::Vector2 p, NS::Math::Vector2 a, NS::Math::Vector2 b)
    {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float lengthSq = abx * abx + aby * aby;
        float t = 0.0f;
        if (lengthSq > 1e-6f)
            t = NS::Math::Clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSq, 0.0f, 1.0f);
        const float dx = p.x - (a.x + abx * t);
        const float dy = p.y - (a.y + aby * t);
        return std::sqrt(dx * dx + dy * dy);
    }

    // 視点の向きを一周させて、 掴めるだけの長さで描かれている矢印が全部引けるか
    // 別の矢印が重なって描かれる角度は、 どちらを採るかの取り決めの話なので別のテストで見る
    TEST(GizmoEditorArrow, SweepViewAngles)
    {
        const NS::Math::Vector3 axes[3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const GizmoAxis axisEnum[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const NS::Math::Size2D viewport{k_ViewWidth, k_ViewHeight};

        for (int pitchDeg = 10; pitchDeg <= 80; pitchDeg += 10)
        {
            for (int yawDeg = 0; yawDeg < 360; yawDeg += 30)
            {
                const float pitch = static_cast<float>(pitchDeg) * k_Pi / 180.0f;
                const float yaw = static_cast<float>(yawDeg) * k_Pi / 180.0f;
                const NS::Math::Vector3 eye{10.0f * std::cos(pitch) * std::sin(yaw),
                                            10.0f * std::sin(pitch),
                                            10.0f * std::cos(pitch) * std::cos(yaw)};
                const NS::Math::Matrix vp = MakeOrbitViewProjection(eye, origin);

                NS::Math::Vector2 origin2d{};
                ASSERT_TRUE(ProjectPoint(vp, origin, origin2d));

                for (int i = 0; i < 3; ++i)
                {
                    NS::Math::Vector2 grab2d{};
                    ASSERT_TRUE(ProjectPoint(vp, origin + axes[i] * 0.4f, grab2d));
                    NS::Math::Vector2 dir{grab2d.x - origin2d.x, grab2d.y - origin2d.y};
                    const float pixels = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    // 画面で潰れている矢印は掴めなくて当然なので飛ばす
                    if (pixels < 20.0f)
                        continue;

                    // 別の矢印が同じ所に重なっているなら、 どちらが勝つかは取り決め次第なので飛ばす
                    bool overlapped = false;
                    for (int j = 0; j < 3; ++j)
                    {
                        if (j == i)
                            continue;
                        NS::Math::Vector2 otherEnd2d{};
                        if (!ProjectPoint(vp, origin + axes[j], otherEnd2d))
                            continue;
                        if (PixelDistanceToSegment(grab2d, origin2d, otherEnd2d) < 6.0f)
                            overlapped = true;
                    }
                    if (overlapped)
                        continue;

                    SCOPED_TRACE(::testing::Message() << "pitch=" << pitchDeg << " yaw=" << yawDeg << " axis=" << i);

                    const GizmoAxis picked = GizmoEditor::ToolHandlePick(
                        origin, NS::Math::Quaternion::Identity, GizmoTool::Move, grab2d, vp, viewport);
                    EXPECT_EQ(picked, axisEnum[i]);

                    dir.x = (dir.x / pixels) * 60.0f;
                    dir.y = (dir.y / pixels) * 60.0f;
                    const NS::Math::Ray rayStart = NS::Editor::ScreenToWorldRay(
                        vp, viewport, static_cast<int>(grab2d.x), static_cast<int>(grab2d.y));
                    const NS::Math::Ray rayNow = NS::Editor::ScreenToWorldRay(
                        vp, viewport, static_cast<int>(grab2d.x + dir.x), static_cast<int>(grab2d.y + dir.y));
                    const NS::Math::Vector3 moved = GizmoEditor::ComputeAxisMove(
                        origin, axisEnum[i], NS::Math::Quaternion::Identity, rayStart, rayNow, false);
                    EXPECT_GT((moved - origin).Dot(axes[i]), 0.05f);
                }
            }
        }
    }

    // 編集カメラの初期姿勢 (方位 0 / 見下ろし 30 度) は Z の矢印が Y の矢印に重なって描かれる
    // 重なりの手前は短い Z、 その先は Y と、 どちらにも手が届くこと
    TEST(GizmoEditorArrow, OverlappedArrowsStayReachable)
    {
        const NS::Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        const float pitch = 30.0f * k_Pi / 180.0f;
        const NS::Math::Vector3 eye{0.0f, 15.0f * std::sin(pitch), -15.0f * std::cos(pitch)};
        const NS::Math::Matrix vp = MakeOrbitViewProjection(eye, origin);
        const NS::Math::Size2D viewport{k_ViewWidth, k_ViewHeight};

        // 掴み判定が使う矢印の長さ。 深さ 15 では 1.5 になる
        const float handleLength = 1.5f;

        NS::Math::Vector2 origin2d{};
        NS::Math::Vector2 yTip2d{};
        NS::Math::Vector2 zTip2d{};
        ASSERT_TRUE(ProjectPoint(vp, origin, origin2d));
        ASSERT_TRUE(ProjectPoint(vp, origin + NS::Math::Vector3{0.0f, handleLength, 0.0f}, yTip2d));
        ASSERT_TRUE(ProjectPoint(vp, origin + NS::Math::Vector3{0.0f, 0.0f, handleLength}, zTip2d));

        // 前提として 2 本が重なっている
        ASSERT_LT(PixelDistanceToSegment(zTip2d, origin2d, yTip2d), 4.0f);

        const NS::Math::Vector2 onZ{(origin2d.x + zTip2d.x) * 0.5f, (origin2d.y + zTip2d.y) * 0.5f};
        EXPECT_EQ(
            GizmoEditor::ToolHandlePick(origin, NS::Math::Quaternion::Identity, GizmoTool::Move, onZ, vp, viewport),
            GizmoAxis::Z);

        // Z の先端より外は Y だけが居る
        const NS::Math::Vector2 beyondZ{zTip2d.x + (yTip2d.x - zTip2d.x) * 0.7f,
                                        zTip2d.y + (yTip2d.y - zTip2d.y) * 0.7f};
        EXPECT_EQ(
            GizmoEditor::ToolHandlePick(origin, NS::Math::Quaternion::Identity, GizmoTool::Move, beyondZ, vp, viewport),
            GizmoAxis::Y);
    }

    TEST(GizmoEditorArrow, XArrowGrabsAndMoves)
    {
        ExpectArrowGrabAndDrag(GizmoAxis::X, NS::Math::Vector3{1.0f, 0.0f, 0.0f});
    }

    TEST(GizmoEditorArrow, YArrowGrabsAndMoves)
    {
        ExpectArrowGrabAndDrag(GizmoAxis::Y, NS::Math::Vector3{0.0f, 1.0f, 0.0f});
    }

    TEST(GizmoEditorArrow, ZArrowGrabsAndMoves)
    {
        ExpectArrowGrabAndDrag(GizmoAxis::Z, NS::Math::Vector3{0.0f, 0.0f, 1.0f});
    }
} // namespace
