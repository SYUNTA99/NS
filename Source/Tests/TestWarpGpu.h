#pragma once

#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3dCommon.h"

//! 試しの間だけ WARP の描画装置を Gpu() に差す。作れない機械では空のまま
struct ScopedWarpGpu
{
    NS::Gfx::ComPtr<ID3D11Device> d3d11Device;
    NS::Gfx::ComPtr<ID3D11DeviceContext> context;

    ScopedWarpGpu()
    {
        const HRESULT hr = ::D3D11CreateDevice(nullptr,
                                               D3D_DRIVER_TYPE_WARP,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0,
                                               D3D11_SDK_VERSION,
                                               d3d11Device.GetAddressOf(),
                                               nullptr,
                                               context.GetAddressOf());
        if (FAILED(hr))
        {
            d3d11Device.Reset();
            context.Reset();
            return;
        }
        NS::Gfx::Gpu().device = d3d11Device.Get();
        NS::Gfx::Gpu().context = context.Get();
    }

    ~ScopedWarpGpu()
    {
        NS::Gfx::Gpu().device = nullptr;
        NS::Gfx::Gpu().context = nullptr;
    }

    [[nodiscard]] bool Valid() const { return d3d11Device != nullptr; }
};
