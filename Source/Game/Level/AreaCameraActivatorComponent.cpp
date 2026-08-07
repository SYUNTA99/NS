#include "Game/Level/AreaCameraActivatorComponent.h"

#include "Runtime/Object/Components/PlacedVirtualCamera.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"

namespace NS::Game::Level
{
    // プレイヤーが動き終わった後 (LateUpdate)、カメラ追従 (+50) が読む前に進入判定へ位置を渡す
    AreaCameraActivatorComponent::AreaCameraActivatorComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::LateUpdate + 10)
    {}

    void AreaCameraActivatorComponent::OnUpdate()
    {
        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;

        // 渡すのは owner の位置。 プレイヤーに載せるので、 これが進入判定の対象になる
        const NS::Core::Vector3 position = Owner()->Root().Position();
        scene->World().ForEachComponent<NS::Object::PlacedVirtualCamera>(
            [&position](NS::Object::PlacedVirtualCamera& placed) { placed.UpdateActivation(position); });
    }
} // namespace NS::Game::Level
