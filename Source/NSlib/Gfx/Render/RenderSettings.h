#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{
    namespace Gfx
    {
        //! シーン全体の標準照明・露出設定
        struct RenderSettings
        {
            Vector3 lightDir{-0.3f, -1.0f, -0.2f};
            Vector3 lightColor{1.0f, 1.0f, 1.0f};
            Vector3 ambientColor{0.30f, 0.34f, 0.40f}; // 空側の環境光 (上向き面)
            Vector3 groundColor{0.24f, 0.21f, 0.18f};  // 地面側の環境光 (下向き面)
            float exposure = 1.35f;                    // トーンマップ前の露出倍率
        };
    } // namespace Gfx
} // namespace NS
