#include "Editor/EditorCameraRig.h"

#include "Runtime/Object/Components/EditorCameraComponent.h"

EditorCameraRig::EditorCameraRig() noexcept
{
    m_editorCam = AddComponent<NS::Object::EditorCameraComponent>();
}
