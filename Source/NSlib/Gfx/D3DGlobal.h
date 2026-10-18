#pragma once

#include "NSlib/Gfx/D3dCommon.h"

namespace NS
{
    namespace Gfx
    {
        //! @brief プロセス唯一の D3D11 device / immediate context を束ねたグローバル
        //! @details 中身は D3DDevice が所有する非所有の観測ポインタで、未構築時は nullptr。直接参照は層内部と
        //! ImGui統合のみ
        struct D3DGlobal
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
        };

        [[nodiscard]] D3DGlobal& Gpu() noexcept;
    } // namespace Gfx
} // namespace NS
