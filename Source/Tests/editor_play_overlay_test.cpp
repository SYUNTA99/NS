#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <cstddef>

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
    editor.DrawSceneViewShapes(shapes, NS::Core::Matrix::Identity);
    const std::size_t lines = shapes.VertexCount();
    const std::size_t faces = shapes.FaceVertexCount();
    EXPECT_GT(lines, std::size_t{0});
    EXPECT_GT(faces, std::size_t{0});

    scene.SetSimulationPaused(true);
    for (int i = 0; i < 5; ++i)
    {
        scene.OnUpdate();
        shapes.Clear();
        editor.DrawSceneViewShapes(shapes, NS::Core::Matrix::Identity);
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
    editor.DrawSceneViewShapes(first, NS::Core::Matrix::Identity);
    ASSERT_GT(first.VertexCount(), std::size_t{0});

    for (int i = 0; i < 100; ++i)
    {
        NS::Gfx::DebugShapes shapes;
        editor.DrawSceneViewShapes(shapes, NS::Core::Matrix::Identity);
        EXPECT_EQ(shapes.VertexCount(), first.VertexCount());
        EXPECT_EQ(shapes.FaceVertexCount(), first.FaceVertexCount());
    }
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}
