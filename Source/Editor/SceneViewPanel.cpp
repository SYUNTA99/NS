#include "Editor/SceneViewPanel.h"

#include "Editor/DragTypes.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Object/Components/EditorCameraComponent.h"
#include "Runtime/Object/Scene/Scene.h"

#include <filesystem>
#include <optional>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    void SceneViewPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        m_editHovered = false;
        m_freeViewHovered = false;
        const bool playMode = editor.CurrentMode() == LevelEditorController::Mode::Play;

        ImVec2 rectMin{};
        ImVec2 rectMax{};
        bool hovered = false;
        if (m_surface.BeginView(k_PanelScene, rectMin, rectMax, hovered))
        {
            // 入力を受け持つのは編集中だけ。 プレイ中は自由視点で見回す
            if (!playMode)
            {
                m_editHovered = hovered;
                editor.SetGameView(static_cast<int>(rectMin.x),
                                   static_cast<int>(rectMin.y),
                                   static_cast<int>(rectMax.x - rectMin.x),
                                   static_cast<int>(rectMax.y - rectMin.y),
                                   hovered);
            }
            else
            {
                m_freeViewHovered = hovered;
            }

            // Assets からの持ち込み口。 メッシュは 1 体として置き、 材質は選択中へ塗る
            if (!playMode && ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_MeshDragType))
                {
                    const char* dropped = static_cast<const char*>(payload->Data);
                    editor.AddObjectWithMesh(std::filesystem::path(dropped));
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_MaterialDragType))
                {
                    const char* dropped = static_cast<const char*>(payload->Data);
                    editor.ApplyMaterialToSelected(std::filesystem::path(dropped));
                }
                ImGui::EndDragDropTarget();
            }
        }
        m_surface.EndView();

        // プレイ中の当たり表示は、 このパネルが映っているフレームだけ積ませる
        editor.SetSceneViewVisible(m_surface.IsVisible());

        // 編集中に自分が裏なら編集オーバーレイと矩形を止める
        if (!playMode && !m_surface.IsVisible())
            editor.HideGameView();

        // 自由視点入力はプレイ中に可視なフレームだけ効かせる
        if (playMode && m_surface.IsVisible())
        {
            TickFreeViewInput(editor);
        }
        else
        {
            m_freeViewLatch = false;
            m_freeFlying = false;
        }
#else
        (void)editor;
#endif
    }

    void SceneViewPanel::Suppress(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        m_freeViewHovered = false;
        m_surface.ResetVisibility();
        m_freeViewLatch = false;
        m_freeFlying = false;
        editor.SetSceneViewVisible(false);
        if (editor.CurrentMode() == LevelEditorController::Mode::Edit)
            editor.HideGameView();
#else
        (void)editor;
#endif
    }

    void SceneViewPanel::ClearForHiddenUi(LevelEditorController& editor) noexcept
    {
        // F5 の全画面直描き中はパネル矩形が無いので予備の全画面矩形へ戻す
        editor.ClearGameView();
        // 全画面はゲーム画面そのものなので当たりの線を出さない
        editor.SetSceneViewVisible(false);
        m_freeViewLatch = false;
        m_freeFlying = false;
    }

    std::optional<NS::Object::SceneView> SceneViewPanel::CollectView(LevelEditorController& editor) noexcept
    {
        return m_surface.CollectView(editor.SceneViewPose());
    }

    bool SceneViewPanel::ConsumeFreeFlyReleased() noexcept
    {
        // 見回しドラッグの立ち下がりで押しっぱなしのキーが残らないよう掃除させる
        const bool released = m_wasFreeFlying && !m_freeFlying;
        m_wasFreeFlying = m_freeFlying;
        return released;
    }

    void SceneViewPanel::UpdateMouseLatch() noexcept
    {
#if NS_EDITOR_ENABLED
        // 全マウスボタン解放中だけ hover に追従し、 パネル発のドラッグ中は離すまで追従を維持する
        if (!ImGui::IsAnyMouseDown())
            m_mouseLatch = m_editHovered;
#endif
    }

    void SceneViewPanel::TickFreeViewInput(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsAnyMouseDown())
            m_freeViewLatch = m_freeViewHovered;

        const bool flying = m_freeViewLatch && ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool panning = m_freeViewLatch && ImGui::IsMouseDown(ImGuiMouseButton_Middle);

        NS::Object::FreeFlightInput input{};
        input.deltaSeconds = io.DeltaTime;
        input.flying = flying;
        if (flying)
        {
            input.lookYawPixels = io.MouseDelta.x;
            input.lookPitchPixels = io.MouseDelta.y;
        }
        if (panning)
        {
            input.panXPixels = io.MouseDelta.x;
            input.panYPixels = io.MouseDelta.y;
        }
        if (m_freeViewHovered)
            input.wheelNotches = io.MouseWheel;

        if (flying && !io.WantTextInput)
        {
            if (io.KeyShift)
                input.speedScale = 4.0f;
            if (ImGui::IsKeyDown(ImGuiKey_W))
                input.forwardAxis += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_S))
                input.forwardAxis -= 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_D))
                input.strafeAxis += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_A))
                input.strafeAxis -= 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_E))
                input.verticalAxis += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_Q))
                input.verticalAxis -= 1.0f;
        }

        m_freeFlying = flying;
        editor.TickPlaySceneView(input);
#else
        (void)editor;
#endif
    }
} // namespace NS::Editor
