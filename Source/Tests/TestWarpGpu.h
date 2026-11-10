#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3DDevice.h"

//! ウィンドウのない WARP の装置をテストの間だけ所有する。構築の失敗は Valid() で確認する
struct ScopedWarpGpu : NS::NonCopyable
{
    NS::Gfx::ComPtr<ID3D11Device> d3d11Device;
    NS::Gfx::ComPtr<ID3D11DeviceContext> context;
    NS::Gfx::D3DDevice device;

    ScopedWarpGpu() : device(NS::Gfx::D3DDeviceDesc{}, D3D_DRIVER_TYPE_WARP)
    {
        if (device.IsValid())
        {
            d3d11Device = device.Native();
            context = device.NativeContext();
        }
    }

    [[nodiscard]] bool Valid() const { return device.IsValid(); }
};
