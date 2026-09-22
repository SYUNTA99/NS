#include "Runtime/Object/Components/VirtualCamera.h"

#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Obj
{
    namespace
    {
        // scene に着いていない裸の GameObject 上でも OnStart は走るので、経路の全段で不在を許す
        [[nodiscard]] CameraBrain* FindBrain(Component& self) noexcept
        {
            GameObject* owner = self.Owner();
            if (owner == nullptr)
            {
				return nullptr;
            }
            Scene* scene = owner->OwningScene();
            if (scene == nullptr)
            {
				return nullptr;
            }
            return scene->CameraBrain();
        }
    } // namespace

    // 仮想デストラクタはヘッダでなくこの .cpp に置き、vtable の重複生成を避ける
    VirtualCamera::~VirtualCamera() noexcept = default;

    void VirtualCamera::OnStart()
    {
        if (CameraBrain* brain = FindBrain(*this))
        {
            brain->AddVirtualCamera(this);
        }
    }

    void VirtualCamera::OnEndPlay()
    {
        if (CameraBrain* brain = FindBrain(*this))
        {
			brain->RemoveVirtualCamera(this);
        }
    }
} // namespace NS::Obj
