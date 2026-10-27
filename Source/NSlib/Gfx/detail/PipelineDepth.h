#pragma once

#include "NSlib/Gfx/D3DPipeline.h"

#include <d3d11.h>

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            [[nodiscard]] inline D3D11_DEPTH_STENCIL_DESC MakeDepthStencilDesc(DepthMode depth) noexcept
            {
                D3D11_DEPTH_STENCIL_DESC dd{};
                dd.StencilEnable = FALSE;
                dd.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
                dd.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
                switch (depth)
                {
                case DepthMode::ReadOnly:
                    dd.DepthEnable = TRUE;
                    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                    break;
                case DepthMode::Occluded:
                    dd.DepthEnable = TRUE;
                    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                    dd.DepthFunc = D3D11_COMPARISON_GREATER;
                    break;
                case DepthMode::Disabled:
                    dd.DepthEnable = FALSE;
                    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
                    break;
                case DepthMode::ReadWrite:
                default:
                    dd.DepthEnable = TRUE;
                    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
                    dd.DepthFunc = D3D11_COMPARISON_LESS;
                    break;
                }
                return dd;
            }
        } // namespace detail
    } // namespace Gfx
} // namespace NS
