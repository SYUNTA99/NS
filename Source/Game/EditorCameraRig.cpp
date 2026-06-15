#include "Game/EditorCameraRig.h"

EditorCameraRig::EditorCameraRig() noexcept
{
    m_editorCam = AddComponent<NS::Scene::EditorCameraComponent>();
}
