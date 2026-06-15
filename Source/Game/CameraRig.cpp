#include "Game/CameraRig.h"

CameraRig::CameraRig(NS::Platform::Input* input,
                     NS::Scene::Transform* followTarget,
                     const NS::Scene::CharacterMovementComponent* movement) noexcept
{
    m_follow = AddComponent<NS::Scene::ThirdPersonFollowComponent>(followTarget);
    m_follow->SetInput(input);
    m_follow->SetMovement(movement);
}
