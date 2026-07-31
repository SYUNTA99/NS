#pragma once

#include "Runtime/Object/Components/EditorCameraComponent.h"
#include "Runtime/Object/GameObject.h"

//! @brief エディタでの操作専用となる自由視点カメラオブジェクト。
//! @details シーン内を自由に移動・観察するためのカメラコンポーネントを保持する。
class EditorCameraRig : public NS::Object::GameObject
{
public:
    EditorCameraRig() noexcept;
    ~EditorCameraRig() override = default;

    //! 自由視点カメラのコンポーネントを取得する
    [[nodiscard]] NS::Object::EditorCameraComponent& EditorCam() noexcept { return *m_editorCam; }

private:
    NS::Object::EditorCameraComponent* m_editorCam = nullptr;
};