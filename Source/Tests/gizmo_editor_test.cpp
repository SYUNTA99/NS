#include "Game/Editor/GizmoEditor.h"

#include <gtest/gtest.h>

namespace
{
    using NS::Game::Editor::GizmoEditor;
    using NS::Game::Editor::GizmoTool;

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
} // namespace
