#include "Game/EditorCameraRig.h"

EditorCameraRig::EditorCameraRig() noexcept
{
    m_camera = AddComponent<NS::Scene::CameraComponent>();
    m_editorCam = AddComponent<NS::Scene::EditorCameraComponent>();
    m_editorCam->SetCamera(m_camera);
}
