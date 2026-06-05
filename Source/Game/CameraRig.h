#pragma once

#include "Framework/Scene/CameraComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/ThirdPersonFollowComponent.h"

namespace NS::Platform
{
    class Input;
}

namespace NS::Scene
{
    class CharacterMovementComponent;
    class Transform;
} // namespace NS::Scene

/// 追従カメラの GameObject
/// CameraComponent + ThirdPersonFollowComponent を GameObject が所有し、
/// コンストラクタ内で follow→camera / follow←input / follow←movement の参照配線を済ませる
/// 入力 / 追従対象 Transform / movement の寿命は呼出側 (LevelEditorScene) が保証する
class CameraRig : public NS::Scene::GameObject
{
public:
    CameraRig(NS::Platform::Input* input,
              NS::Scene::Transform* followTarget,
              const NS::Scene::CharacterMovementComponent* movement) noexcept;
    ~CameraRig() override = default;

    CameraRig(const CameraRig&) = delete;
    CameraRig& operator=(const CameraRig&) = delete;
    CameraRig(CameraRig&&) = delete;
    CameraRig& operator=(CameraRig&&) = delete;

    [[nodiscard]] NS::Scene::CameraComponent& Camera() noexcept { return *m_camera; }
    [[nodiscard]] NS::Scene::ThirdPersonFollowComponent& Follow() noexcept { return *m_follow; }

private:
    NS::Scene::CameraComponent* m_camera = nullptr;
    NS::Scene::ThirdPersonFollowComponent* m_follow = nullptr;
};
