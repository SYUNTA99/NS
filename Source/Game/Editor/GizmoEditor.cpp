#include "Game/Editor/GizmoEditor.h"

namespace NS::Game::Editor
{
    void GizmoEditor::SetSelectableObjects(std::span<NS::Scene::GameObject* const> objects,
                                           std::span<const NS::Math::Vector3> localHalfExtents) noexcept
    {
        m_objects = objects;
        m_halfExtents = localHalfExtents;
    }

    void GizmoEditor::Tick(const NS::Math::Matrix&, NS::Math::Size2D) noexcept {}

    void GizmoEditor::Render(const NS::Math::Matrix&, NS::Math::Size2D) noexcept {}

    bool GizmoEditor::Undo() noexcept
    {
        return false;
    }

    bool GizmoEditor::Redo() noexcept
    {
        return false;
    }

    int GizmoEditor::PickNearestObb(const NS::Math::Ray&,
                                    std::span<const NS::Math::Matrix>,
                                    std::span<const NS::Math::Vector3>) noexcept
    {
        return -1;
    }

    NS::Math::Vector3 GizmoEditor::ComputeAxisMove(
        const NS::Math::Vector3& startPos, GizmoAxis, const NS::Math::Ray&, const NS::Math::Ray&, bool) noexcept
    {
        return startPos;
    }

    float GizmoEditor::ScreenDragToAngle(NS::Math::Vector2, NS::Math::Vector2, NS::Math::Vector2) noexcept
    {
        return 0.0f;
    }

    NS::Math::Quaternion GizmoEditor::ComputeAxisRotate(const NS::Math::Quaternion& startRot,
                                                        GizmoAxis,
                                                        float,
                                                        bool) noexcept
    {
        return startRot;
    }

    float GizmoEditor::ScreenDragToScaleAmount(NS::Math::Vector2, NS::Math::Vector2) noexcept
    {
        return 0.0f;
    }

    NS::Math::Vector3 GizmoEditor::ComputeScale(const NS::Math::Vector3& startScale, GizmoAxis, float, bool) noexcept
    {
        return startScale;
    }

    void GizmoEditor::ApplyDragForTest(
        const NS::Math::Matrix&, NS::Math::Size2D, GizmoAxis, NS::Math::Vector2, NS::Math::Vector2) noexcept
    {}

    void GizmoEditor::OnToolKey(NS::Platform::Key) noexcept {}

    GizmoAxis GizmoEditor::PickHandle(NS::Math::Vector2, const NS::Math::Matrix&, NS::Math::Size2D) const noexcept
    {
        return GizmoAxis::None;
    }
} // namespace NS::Game::Editor
