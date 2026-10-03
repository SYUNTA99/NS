#include "Editor/LevelEditorController.h"
#include "Editor/PlacementCatalog.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <cstddef>

// プレイ中に Scene タブへ出す当たり線と相手の正面の面を、固定ステップの更新の後に溜め直すことを縛る
// 描画の時に溜める道は Application が居ないと何もしないので、ここでは見ない

namespace
{
    namespace DD = NS::Gfx::DebugDraw;

    // 当たりと赤の欄を持つ置物を 1 体置き、プレイへ入って Scene タブが映っている所まで進める
    void EnterPlayWithMapObj(LevelEditorController& editor)
    {
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem("置物");
        ASSERT_NE(item, nullptr);
        editor.PlaceItem(*item);
        editor.EnterPlay();
        editor.SetSceneViewVisible(true);
    }
} // namespace

// 固定ステップの頭で前の図形が捨てられても、同じステップの後に溜め直して Scene タブの描画まで残す
TEST(EditorPlayOverlay, SteppedFrameKeepsLinesAndFacesForTheSceneTab)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    ASSERT_NO_FATAL_FAILURE(EnterPlayWithMapObj(editor));
    DD::Clear();

    scene.OnUpdate();
    const std::size_t linesFromStep = DD::VertexCount();
    const std::size_t facesFromStep = DD::FaceVertexCount();
    editor.QueuePlayOverlaysAfterStep();

    EXPECT_GT(DD::VertexCount(), linesFromStep);
    EXPECT_GT(DD::FaceVertexCount(), facesFromStep);
    DD::Clear();
}

// 一時停止中も固定ステップの更新は回るが、世界が進まないので捨てられない。そこで溜めると同じ面が重なって濃くなる
TEST(EditorPlayOverlay, PausedStepsDoNotPileUpLinesOrFaces)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    ASSERT_NO_FATAL_FAILURE(EnterPlayWithMapObj(editor));
    DD::Clear();
    scene.OnUpdate();
    editor.QueuePlayOverlaysAfterStep();
    const std::size_t lines = DD::VertexCount();
    const std::size_t faces = DD::FaceVertexCount();
    ASSERT_GT(faces, std::size_t{0});

    scene.SetSimulationPaused(true);
    for (int i = 0; i < 5; ++i)
    {
        scene.OnUpdate();
        editor.QueuePlayOverlaysAfterStep();
    }
    EXPECT_EQ(DD::VertexCount(), lines);
    EXPECT_EQ(DD::FaceVertexCount(), faces);
    DD::Clear();
}

// Scene タブが映っていない間は溜めない。溜めた分は先頭のビューで描かれずにゲーム画面へ出る
TEST(EditorPlayOverlay, HiddenSceneTabQueuesNothing)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    ASSERT_NO_FATAL_FAILURE(EnterPlayWithMapObj(editor));
    editor.SetSceneViewVisible(false);
    DD::Clear();

    scene.OnUpdate();
    const std::size_t linesFromStep = DD::VertexCount();
    const std::size_t facesFromStep = DD::FaceVertexCount();
    editor.QueuePlayOverlaysAfterStep();

    EXPECT_EQ(DD::VertexCount(), linesFromStep);
    EXPECT_EQ(DD::FaceVertexCount(), facesFromStep);
    DD::Clear();
}
