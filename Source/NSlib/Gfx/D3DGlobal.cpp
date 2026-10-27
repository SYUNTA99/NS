#include "NSlib/Gfx/D3DGlobal.h"

namespace NS::Gfx
{
        namespace
        {
            // プロセス唯一の device / immediate context を束ねたグローバル。D3DDevice 構築で代入し、
            // 破棄で nullptr に戻す。D3DBuffer / D3DTexture / D3DShader 等のリソース生成は Gpu() で引く
            D3DGlobal g_d3dGlobal;
        } // namespace

        D3DGlobal& Gpu() noexcept
        {
            return g_d3dGlobal;
        }
} // namespace NS::Gfx
