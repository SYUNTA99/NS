#include "Runtime/Object/Components/VirtualCameraComponent.h"

#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Object
{
    namespace
    {
        // scene に着いていない裸の GameObject 上でも OnStart は走るので、 経路の全段で不在を許す
        [[nodiscard]] CameraBrainComponent* FindBrain(Component& self) noexcept
        {
            GameObject* owner = self.Owner();
            if (owner == nullptr)
                return nullptr;
            Scene* scene = owner->OwningScene();
            if (scene == nullptr)
                return nullptr;
            return scene->CameraBrain();
        }
    } // namespace

    // 仮想デストラクタはヘッダでなくこの .cpp に置き、 vtable の重複生成を避ける
    VirtualCameraComponent::~VirtualCameraComponent() noexcept = default;

    void VirtualCameraComponent::OnStart()
    {
        if (CameraBrainComponent* brain = FindBrain(*this))
            brain->AddVirtualCamera(this);
    }

    void VirtualCameraComponent::OnEndPlay()
    {
        if (CameraBrainComponent* brain = FindBrain(*this))
            brain->RemoveVirtualCamera(this);
    }
} // namespace NS::Object
