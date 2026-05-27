#include "Game/EditorCameraRig.h"

EditorCameraRig::EditorCameraRig() noexcept : m_camera(this), m_editorCam(this)
{
    m_editorCam.SetCamera(&m_camera);
}
