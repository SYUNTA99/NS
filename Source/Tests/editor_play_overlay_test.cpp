#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Scene タブを描く時に渡される溜め場へ、エディタが図形を積むことを縛る
// どのビューにこの口が付くかは ImGui のパネルが決めるので、ここでは見ない

namespace
{
    namespace DD = NS::Gfx::DebugDraw;

    // 当たりと赤の欄を持つ置物を 1 体置く。置いた物は選ばれる
    void PlaceMapObj(LevelEditorController& editor)
    {
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
    }
} // namespace

// 世界が 1 歩も進まない間 (プレイに入った直後と一時停止中) も、描くたびに当たり線と正面の面が出る
TEST(EditorSceneViewShapes, PlayShapesNeedNoStepAndStayOutOfTheStepShapes)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    ASSERT_NO_FATAL_FAILURE(PlaceMapObj(editor));
    editor.EnterPlay();
    DD::Clear();

    NS::Gfx::DebugShapes shapes;
    editor.DrawSceneViewShapes(shapes, NS::Matrix::Identity);
    const std::size_t lines = shapes.VertexCount();
    const std::size_t faces = shapes.FaceVertexCount();
    EXPECT_GT(lines, std::size_t{0});
    EXPECT_GT(faces, std::size_t{0});

    scene.SetSimulationPaused(true);
    for (int i = 0; i < 5; ++i)
    {
        scene.OnUpdate();
        shapes.Clear();
        editor.DrawSceneViewShapes(shapes, NS::Matrix::Identity);
        EXPECT_EQ(shapes.VertexCount(), lines);
        EXPECT_EQ(shapes.FaceVertexCount(), faces);
    }
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}

// 編集中は世界が進まず歩の頭で捨てる者が居ない。描くたびに作って捨てるので何度描いても溜まらない
TEST(EditorSceneViewShapes, EditShapesDoNotPileUpOverManyDraws)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    ASSERT_NO_FATAL_FAILURE(PlaceMapObj(editor));
    DD::Clear();

    NS::Gfx::DebugShapes first;
    editor.DrawSceneViewShapes(first, NS::Matrix::Identity);
    ASSERT_GT(first.VertexCount(), std::size_t{0});

    for (int i = 0; i < 100; ++i)
    {
        NS::Gfx::DebugShapes shapes;
        editor.DrawSceneViewShapes(shapes, NS::Matrix::Identity);
        EXPECT_EQ(shapes.VertexCount(), first.VertexCount());
        EXPECT_EQ(shapes.FaceVertexCount(), first.FaceVertexCount());
    }
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}

// 枠は主対象以外の選択物にだけ出る。主対象はギズモで見えるので枠を重ねない
TEST(EditorSceneViewShapes, SelectionOutlinesLeaveOutThePrimary)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    ASSERT_NO_FATAL_FAILURE(PlaceMapObj(editor));
    const std::uint32_t first = editor.SelectedObjectId();
    ASSERT_NO_FATAL_FAILURE(PlaceMapObj(editor));
    const std::uint32_t second = editor.SelectedObjectId();

    const auto countLines = [&editor](std::vector<std::uint32_t> ids) {
        editor.SelectObjects(std::move(ids), NS::Obj::k_NoObjectId);
        NS::Gfx::DebugShapes shapes;
        editor.DrawSceneViewShapes(shapes, NS::Matrix::Identity);
        return shapes.VertexCount();
    };
    const std::size_t none = countLines({});
    const std::size_t onlyFirst = countLines({first});
    const std::size_t onlySecond = countLines({second});
    editor.SelectObjects({first, second}, second);
    NS::Gfx::DebugShapes both;
    editor.DrawSceneViewShapes(both, NS::Matrix::Identity);

    // 1 体ずつ選んだ分を引いた残りが枠。箱の枠は辺 12 本で頂点 24
    const std::size_t outlines = both.VertexCount() + none - onlyFirst - onlySecond;
    EXPECT_EQ(outlines, std::size_t{24});
}
