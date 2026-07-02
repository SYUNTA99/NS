#include "Game/CameraRig.h"

CameraRig::CameraRig(NS::Scene::Transform* followTarget, const NS::Scene::CharacterMovementComponent* movement) noexcept
{
    m_follow = AddComponent<NS::Scene::ThirdPersonFollowComponent>(followTarget);
    m_follow->SetMovement(movement);
}
