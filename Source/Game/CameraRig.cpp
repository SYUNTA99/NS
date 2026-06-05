#include "Game/CameraRig.h"

CameraRig::CameraRig(NS::Platform::Input* input,
                     NS::Scene::Transform* followTarget,
                     const NS::Scene::CharacterMovementComponent* movement) noexcept
{
    m_camera = AddComponent<NS::Scene::CameraComponent>();
    m_follow = AddComponent<NS::Scene::ThirdPersonFollowComponent>(followTarget);
    m_follow->SetCamera(m_camera);
    m_follow->SetInput(input);
    m_follow->SetMovement(movement);
}
