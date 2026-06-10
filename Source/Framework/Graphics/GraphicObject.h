#pragma once

/// @file GraphicObject.h
/// @brief NS::Graphics — プロセス唯一の D3D11 device / immediate context を束ねたグローバル

#include <Framework/Graphics/D3dCommon.h>

namespace NS::Graphics
{
    /// プロセス唯一の D3D11 device と immediate context を束ねたグローバルハンドル
    /// Renderer 構築で代入、 破棄で nullptr に戻る。 未構築時は両方 nullptr
    /// 実体は Renderer の ComPtr が所有し、 ここは非所有の観測ポインタ
    /// リソース生成は型ごとの Create() を使い、 ここを直接参照するのは層内部 / 外部 SDK 連携に限る
    struct GraphicObject
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
    };

    /// プロセス唯一の GraphicObject を返す
    [[nodiscard]] GraphicObject& Gpu() noexcept;
} // namespace NS::Graphics
