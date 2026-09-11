#pragma once

// D3D11_USAGE から CreateBuffer / CreateTexture 用の派生値を求める内部ヘルパ

#include <d3d11.h>

namespace NS::Graphics::detail
{

    //! D3D11_USAGE に対し D3D11 が許す CPUAccessFlags を返す。 両者は仕様で一意対応し矛盾すると Create が失敗する
    //! DEFAULT / IMMUTABLE は 0、 DYNAMIC は WRITE、 STAGING は READ | WRITE
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

} // namespace NS::Graphics::detail
