#include "Editor/GameViewPanel.h"

#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Object/Scene/Scene.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    void GameViewPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        m_hovered = false;
        const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;

        ImVec2 rectMin{};
        ImVec2 rectMax{};
        bool hovered = false;
        if (m_surface.BeginView(k_PanelGame, rectMin, rectMax, hovered))
        {
            // 入力を受け持つのはプレイ中だけ。 編集中はゲームカメラを映すが操作は Scene 側
            if (playMode)
            {
                m_hovered = hovered;
                editor.SetGameView(static_cast<int>(rectMin.x),
                                   static_cast<int>(rectMin.y),
                                   static_cast<int>(rectMax.x - rectMin.x),
                                   static_cast<int>(rectMax.y - rectMin.y),
                                   hovered);
            }
        }
        m_surface.EndView();

        // 入力を持つべきプレイ中に自分が裏なら、 編集入力とオーバーレイを止める
        if (playMode && !m_surface.IsVisible())
            editor.HideGameView();
#else
        (void)editor;
#endif
    }

    void GameViewPanel::Suppress(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        m_hovered = false;
        m_surface.ResetVisibility();
        if (editor.CurrentMode() == LevelEditorController::Mode::Play)
            editor.HideGameView();
#else
        (void)editor;
#endif
    }

    std::optional<NS::Object::SceneView> GameViewPanel::CollectView(LevelEditorController& editor) noexcept
    {
        return m_surface.CollectView(editor.GameViewPose());
    }

    void GameViewPanel::UpdateMouseLatch() noexcept
    {
#if NS_EDITOR_ENABLED
        // 全マウスボタン解放中だけ hover に追従し、 パネル発のドラッグ中は離すまで追従を維持する
        if (!ImGui::IsAnyMouseDown())
            m_mouseLatch = m_hovered;
#endif
    }
} // namespace NS::Editor
