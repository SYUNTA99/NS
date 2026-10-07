#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Windows/Input.h"

#include <gtest/gtest.h>

#include <optional>

namespace
{
    NS::Matrix MakeViewProjection(const NS::Editor::ViewRect& view)
    {
        const NS::Matrix viewMatrix =
            NS::Matrix::CreateLookAt(NS::Vector3{3.0f, 3.0f, -5.0f}, NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3::Up);
        const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
        return viewMatrix * NS::Matrix::CreatePerspectiveFieldOfView(1.0f, aspect, 0.1f, 100.0f);
    }

    std::optional<NS::Vector2> FindMoveHandle(const NS::Matrix& viewProjection, const NS::Editor::ViewRect& view)
    {
        const NS::Size2D viewport = NS::Editor::ViewRectSize(view);
        for (int y = 0; y < view.height; ++y)
        {
            for (int x = 0; x < view.width; ++x)
            {
                const NS::Vector2 point{static_cast<float>(x), static_cast<float>(y)};
                const NS::Editor::GizmoAxis axis = NS::Editor::GizmoEditor::ToolHandlePick(NS::Vector3::Zero,
                                                                                           NS::Quaternion::Identity,
                                                                                           NS::Editor::GizmoTool::Move,
                                                                                           point,
                                                                                           viewProjection,
                                                                                           viewport);
                if (axis != NS::Editor::GizmoAxis::None)
                {
                    return point;
                }
            }
        }
        return std::nullopt;
    }
} // namespace

// 休ませたギズモは引き続けない。モードを移る間に引きが残ると、組み直した後に消えた物を動かす
TEST(EditorGizmo, DeactivatingEndsTheDrag)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    input.Mouse().ClearState();
    const int startX = input.Mouse().GetX();
    const int startY = input.Mouse().GetY();
    const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
    const NS::Matrix viewProjection = MakeViewProjection(view);
    const std::optional<NS::Vector2> handle = FindMoveHandle(viewProjection, view);
    ASSERT_TRUE(handle.has_value());

    NS::Obj::Transform target;
    NS::Editor::GizmoEditor gizmo;
    gizmo.SetInput(&input);
    gizmo.SetActive(true);
    gizmo.SetSelected(&target);
    input.Mouse().OnMove(static_cast<int>(handle->x), static_cast<int>(handle->y));
    input.Mouse().OnButtonDown(NS::OS::MouseButton::Left);
    gizmo.Tick(viewProjection, view, true);
    ASSERT_TRUE(gizmo.IsDragging());

    gizmo.SetActive(false);
    EXPECT_FALSE(gizmo.IsDragging());
    // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
    input.Mouse().OnMove(startX, startY);
    input.Mouse().ClearState();
    input.Mouse().Update();
}
