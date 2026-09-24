#include "Game/Player/PlayerCameraSpawner.h"

#include "Game/Level/FollowCameraFeed.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectRef.h"
#include "Runtime/Object/Scene/Scene.h"

#include <cstdint>
#include <memory>

namespace NS::Game::Player
{
    // やり直し (LateUpdate + 10) の後、追従カメラへ値を渡す FollowCameraFeed (LateUpdate + 40) の前
    PlayerCameraSpawner::PlayerCameraSpawner() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate + 20)
    {}

    void PlayerCameraSpawner::OnUpdate()
    {
        if (Owner() == nullptr || Owner()->OwningScene() == nullptr)
        {
            return;
        }

        // 未採番の 0 同士を突き合わせると、誰も指していないカメラが持ち主を追っている扱いになる
        const std::uint32_t ownerId = Owner()->Id();
        if (ownerId == 0)
        {
            return;
        }

        NS::Obj::Scene& scene = *Owner()->OwningScene();
        bool followed = false;
        scene.Objects().ForEachComponent<NS::Obj::ThirdPersonFollow>([ownerId, &followed](NS::Obj::ThirdPersonFollow& follow) {
            if (follow.TargetRef().id == ownerId)
            {
                followed = true;
            }
        });
        if (followed)
        {
            return;
        }

        // 一時オブジェクトにしないのは、データからの組み直しで持ち主と一緒に消えるようにするため
        // 残すと、作り直された持ち主の代わりに古い持ち主の Transform を指したままになる
        auto camera = std::make_unique<NS::Obj::GameObject>();
        auto* follow = camera->AddComponent<NS::Obj::ThirdPersonFollow>();
        follow->SetTargetRef(NS::Obj::ObjectRef{ownerId});
        // 追従カメラは休止で生まれ、プレイ突入時に起こされる。ここで足すのはプレイ中なので自分で起こす
        follow->SetActive(true);
        camera->AddComponent<NS::Game::Level::FollowCameraFeed>();
        scene.SpawnObject(std::move(camera), "Player Camera");
    }
} // namespace NS::Game::Player
