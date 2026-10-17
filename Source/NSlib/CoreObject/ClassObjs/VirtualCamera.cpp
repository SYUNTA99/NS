#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"

#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    bool VirtualCamera::IsOwnerAppeared() const noexcept
    {
        if (m_owner == nullptr)
        {
            return true;
        }
        return m_owner->IsAppeared();
    }
} // namespace NS
