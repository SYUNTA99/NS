#include "Framework/Graphics/DebugDraw.h"
#include "Editor/EditorMode.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;

TEST(CursorPreview, RendersAABBToDebugDrawWhenCursorValid)
{
    NS::GameCore::Level::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetActive(true);

    EditorNs::EditorMode::CursorState state;
    state.valid = true;
    state.placementCenter = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
    state.placementBlocked = false;
    editor.SetCursorForTest(state);

    NS::Graphics::DebugDraw::Clear();
    editor.RenderCursorPreview();

    EXPECT_GT(NS::Graphics::DebugDraw::VertexCount(), 0u);
}

TEST(CursorPreview, DoesNothingWhenCursorInvalid)
{
    NS::GameCore::Level::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetActive(true);

    EditorNs::EditorMode::CursorState state;
    state.valid = false;
    editor.SetCursorForTest(state);

    NS::Graphics::DebugDraw::Clear();
    editor.RenderCursorPreview();

    EXPECT_EQ(NS::Graphics::DebugDraw::VertexCount(), 0u);
}

TEST(CursorPreview, DoesNothingWhenInactive)
{
    NS::GameCore::Level::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetActive(false);

    EditorNs::EditorMode::CursorState state;
    state.valid = true;
    state.placementCenter = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
    editor.SetCursorForTest(state);

    NS::Graphics::DebugDraw::Clear();
    editor.RenderCursorPreview();

    EXPECT_EQ(NS::Graphics::DebugDraw::VertexCount(), 0u);
}
