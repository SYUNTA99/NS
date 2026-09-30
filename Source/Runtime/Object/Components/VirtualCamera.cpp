#include "Runtime/Object/Components/VirtualCamera.h"

#include "Runtime/Object/Actor.h"

namespace NS::Obj
{
    // 仮想デストラクタはヘッダでなくこの .cpp に置き、vtable の重複生成を避ける
    VirtualCamera::~VirtualCamera() noexcept = default;

    void VirtualCamera::OnStart()
    {
        // scene に着いていない裸の Actor 上でも OnStart は走る。管理役が無ければ窓口が何もしない
        if (Owner() != nullptr)
        {
            RegisterVirtualCamera(*Owner(), this);
        }
    }

    void VirtualCamera::OnEndPlay()
    {
        if (Owner() != nullptr)
        {
            UnregisterVirtualCamera(*Owner(), this);
        }
    }
} // namespace NS::Obj
