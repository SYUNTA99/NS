#pragma once

#include "NSlib/Gfx/D3DDevice.h"

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            // GPU の無い読み込みやテストでは nullptr。所有者から取り、別の場所には保持しない
            [[nodiscard]] inline ID3D11Device* CurrentDevice() noexcept
            {
                const D3DDevice* device = D3DDevice::Current();
                if (device == nullptr)
                {
                    return nullptr;
                }
                return device->Native();
            }

            [[nodiscard]] inline ID3D11DeviceContext* CurrentContext() noexcept
            {
                const D3DDevice* device = D3DDevice::Current();
                if (device == nullptr)
                {
                    return nullptr;
                }
                return device->NativeContext();
            }
        } // namespace detail
    } // namespace Gfx
} // namespace NS
