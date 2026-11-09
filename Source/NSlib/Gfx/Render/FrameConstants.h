#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/RenderSettings.h"

#include <array>

namespace NS
{
    namespace Gfx
    {
        //! 1 描画あたりの定数バッファ (standard.vs / standard.ps とレイアウト一致、行優先・左手系)
        struct alignas(16) FrameCB
        {
            Matrix world{};
            Matrix viewProj{};
            // 照明の既定値はプロジェクト描画既定値の RenderSettings と共有し、値の二重管理を避ける
            Vector3 lightDir = Gfx::RenderSettings{}.lightDir;
            float groundWaveCenterX = 0.0f; // 床の波の中心のワールド X 座標
            Vector3 baseColor{1.0f, 1.0f, 1.0f};
            float groundWaveCenterZ = 0.0f; // 床の波の中心のワールド Z 座標
            Vector3 lightColor = Gfx::RenderSettings{}.lightColor;
            float groundWaveRadius = 0.0f; // 床の波の半径 (m)
            Vector3 ambientColor = Gfx::RenderSettings{}.ambientColor;
            float groundWaveStrength = 0.0f; // 床の波の強さ (0 以下で無効)
            Vector3 groundColor = Gfx::RenderSettings{}.groundColor;
            float exposure = Gfx::RenderSettings{}.exposure;
            // 名前の無い物ごとの 4 つの値。意味は Model の SetDrawValues で書く物と、シェーダの口のファイル SurfaceHooks.hlsli が決める
            std::array<Vector4, 4> objectValues{};
            float opacity = 1.0f; // 不透明度 0..1 (lit / unlit / soft_circle が読む。Alpha ブレンド時のみ有効)
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
            Vector3 cameraPosition{};
            float sceneTimeSeconds = 0.0f;
        };
        static_assert(sizeof(FrameCB) == 304, "FrameCB size は Common.hlsli と完全一致 (304 byte)");
        static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");
    } // namespace Gfx
} // namespace NS
