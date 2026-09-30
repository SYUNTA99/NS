#include "Game/Level/FollowCamera.h"

#include "Game/Level/FollowCameraFeed.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    FollowCamera::FollowCamera() noexcept
    {
        AddComponent<NS::Obj::ThirdPersonFollow>();
        AddComponent<FollowCameraFeed>();
    }

    NS_PLACEABLE(FollowCamera, "追従カメラ")
} // namespace NS::Game::Level
