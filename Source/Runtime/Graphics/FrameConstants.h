#pragma once

#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Core/Math.h"

namespace NS::Graphics
{
    /// @brief per-draw 定数バッファ。standard.{vs,ps} と完全一致で sizeof=192、row_major LH
    /// @details world / viewProj のオブジェクト単位値に lighting を束ねる。Material の内蔵 CB へ流す
    struct alignas(16) FrameCB
    {
        NS::Core::Matrix world{};
        NS::Core::Matrix viewProj{};
        // lighting の既定値はプロジェクト描画既定値の RenderSettings と共有し、値の二重管理を避ける
        NS::Core::Vector3 lightDir = NS::Graphics::RenderSettings{}.lightDir;
        float pad0 = 0.0f;
        NS::Core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        float pad1 = 0.0f;
        NS::Core::Vector3 lightColor = NS::Graphics::RenderSettings{}.lightColor;
        float pad2 = 0.0f;
        NS::Core::Vector3 ambientColor = NS::Graphics::RenderSettings{}.ambientColor;
        float pad3 = 0.0f;
        NS::Core::Vector3 groundColor = NS::Graphics::RenderSettings{}.groundColor;
        float exposure = NS::Graphics::RenderSettings{}.exposure;
    };
    static_assert(sizeof(FrameCB) == 208, "FrameCB size は standard.vs と完全一致 (208 byte)");
    static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");

} // namespace NS::Graphics
