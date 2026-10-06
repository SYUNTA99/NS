#include "NSlib/Object/Components/VirtualCamera.h"

#include "NSlib/Object/Actor.h"

namespace NS::Obj
{
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
