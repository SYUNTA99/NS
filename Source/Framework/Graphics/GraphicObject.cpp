#include "Framework/Graphics/GraphicObject.h"

namespace NS::Graphics
{
    namespace
    {
        // プロセス唯一の device / immediate context を束ねたグローバル。Renderer 構築で代入し、
        // 破棄で nullptr に戻す。Buffer / Texture / Shader 等のリソース生成は Gpu() で引く
        GraphicObject g_graphicObject;
    } // namespace

    GraphicObject& Gpu() noexcept
    {
        return g_graphicObject;
    }
} // namespace NS::Graphics
