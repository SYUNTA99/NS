#pragma once

/// @file EditorCameraRig.h
/// @brief 編集モード用の自由視点カメラ GameObject
///
/// @details 描画元の `CameraComponent` と、 Mouse / Gamepad 操作で
/// Orbit / Pan / Zoom を駆動する `EditorCameraComponent` を GameObject が所有する
/// レベル側の追従カメラ配置物と並列に LevelEditorController が所有し、
/// mode toggle で active な側を切替える


class EditorCameraRig : public NS::Scene::GameObject
{
public:
    EditorCameraRig() noexcept;
    ~EditorCameraRig() override = default;

    EditorCameraRig(const EditorCameraRig&) = delete;
    EditorCameraRig& operator=(const EditorCameraRig&) = delete;
    EditorCameraRig(EditorCameraRig&&) = delete;
    EditorCameraRig& operator=(EditorCameraRig&&) = delete;

    [[nodiscard]] NS::Scene::EditorCameraComponent& EditorCam() noexcept { return *m_editorCam; }

private:
    NS::Scene::EditorCameraComponent* m_editorCam = nullptr;
};
