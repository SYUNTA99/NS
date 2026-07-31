#include "Editor/EditorMode.h"
#include "Runtime/Graphics/DebugDraw.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;

TEST(CursorPreview, RendersAABBToDebugDrawWhenCursorValid)
{
    EditorNs::EditorMode editor;
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
    EditorNs::EditorMode editor;
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
    EditorNs::EditorMode editor;
    editor.SetActive(false);

    EditorNs::EditorMode::CursorState state;
    state.valid = true;
    state.placementCenter = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
    editor.SetCursorForTest(state);

    NS::Graphics::DebugDraw::Clear();
    editor.RenderCursorPreview();

    EXPECT_EQ(NS::Graphics::DebugDraw::VertexCount(), 0u);
}
