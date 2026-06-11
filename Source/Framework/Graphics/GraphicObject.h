#pragma once

/// @file GraphicObject.h
/// @brief NS::Graphics — プロセス唯一の D3D11 device / immediate context を束ねたグローバル

#include <Framework/Graphics/D3dCommon.h>

namespace NS::Graphics
{
    /// 非所有の観測ポインタ (Renderer が所有、未構築時は nullptr)。直接参照は層内部のみ
    struct GraphicObject
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
    };

    /// プロセス唯一の GraphicObject を返す
    [[nodiscard]] GraphicObject& Gpu() noexcept;
} // namespace NS::Graphics
