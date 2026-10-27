#pragma once

#include <d3d11.h>

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            /// D3D11_USAGE に対し D3D11 仕様が一意に要求する CPUAccessFlags を返す
            [[nodiscard]] inline UINT GetCpuAccessFlags(D3D11_USAGE usage) noexcept
            {
                switch (usage)
                {
                case D3D11_USAGE_DYNAMIC:
                    return D3D11_CPU_ACCESS_WRITE;
                case D3D11_USAGE_STAGING:
                    return D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
                case D3D11_USAGE_DEFAULT:
                case D3D11_USAGE_IMMUTABLE:
                default:
                    return 0u;
                }
            }
        } // namespace detail
    } // namespace Gfx
} // namespace NS
