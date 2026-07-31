#include "Runtime/Object/Components/VirtualCameraComponent.h"

namespace NS::Object
{
    // 仮想デストラクタはヘッダでなくこの .cpp に置き、 vtable の重複生成を避ける
    VirtualCameraComponent::~VirtualCameraComponent() noexcept = default;
} // namespace NS::Object
