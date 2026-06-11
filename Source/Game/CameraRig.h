#pragma once

#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Components/ThirdPersonFollowComponent.h"

namespace NS::Platform
{
    class Input;
}

namespace NS::Scene
{
    class CharacterMovementComponent;
    class Transform;
} // namespace NS::Scene

/// 追従カメラ。 CameraComponent + ThirdPersonFollowComponent を所有する
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
