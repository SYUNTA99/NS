#pragma once

#include "Runtime/Graphics/D3dCommon.h"

namespace NS::Graphics
{
    //! @brief プロセス唯一の D3D11 device / immediate context を束ねたグローバル
    //! @details 中身は Renderer が所有する非所有の観測ポインタで、 未構築時は nullptr。直接参照は層内部と ImGui統合のみ
    struct GraphicObject
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
    };

    //! GraphicObject を返す
    [[nodiscard]] GraphicObject& Gpu() noexcept;
} // namespace NS::Graphics
