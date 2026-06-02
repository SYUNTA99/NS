#pragma once

/// @file EditorCameraRig.h
/// @brief 編集モード用の自由視点カメラ GameObject
///
/// @details `CameraComponent` (描画 source) と `EditorCameraComponent`
/// (Mouse / Gamepad 操作で Orbit / Pan / Zoom を駆動) を named member として保有する
/// `CameraRig` (Player 追従) と並列に LevelEditorScene が unique_ptr で保有し、
/// mode toggle で active な側を切替える。 構造は CameraRig と同じ

#include "Framework/Scene/CameraComponent.h"
#include "Framework/Scene/EditorCameraComponent.h"
#include "Framework/Scene/GameObject.h"

class EditorCameraRig : public NS::Scene::GameObject
{
public:
    EditorCameraRig() noexcept;
    ~EditorCameraRig() override = default;

    EditorCameraRig(const EditorCameraRig&) = delete;
    EditorCameraRig& operator=(const EditorCameraRig&) = delete;
    EditorCameraRig(EditorCameraRig&&) = delete;
    EditorCameraRig& operator=(EditorCameraRig&&) = delete;

    [[nodiscard]] NS::Scene::CameraComponent& Camera() noexcept { return m_camera; }
    [[nodiscard]] NS::Scene::EditorCameraComponent& EditorCam() noexcept { return m_editorCam; }

private:
    NS::Scene::CameraComponent m_camera;
    NS::Scene::EditorCameraComponent m_editorCam;
};
