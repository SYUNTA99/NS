#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/RenderSettings.h"

namespace NS
{
    namespace Gfx
    {
        //! 衝突点からの距離に応じて遅れる頂点振動 (Shaders/Common.hlsli の TremorOffset と一致)
        struct alignas(16) TremorCB
        {
            Vector3 contactOffset{};         // ワールド行列の位置からの衝突点オフセット (ワールド空間、m)
            float amplitude = 0.0f;          // 振幅 (m、0 で無効)
            Vector3 right{1.0f, 0.0f, 0.0f}; // 画面右方向のワールド単位ベクトル
            float elapsedFrames = 0.0f;      // 振動開始からの経過フレーム数
            Vector3 up{0.0f, 1.0f, 0.0f};    // 画面上方向のワールド単位ベクトル
            float framesPerMeter = 0.0f;     // 衝突点から 1 m あたりの開始遅延フレーム数
            float ringFrames = 0.0f;         // 1 箇所が振動するフレーム数 (0 以下で無効)
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };
        static_assert(sizeof(TremorCB) == 64, "TremorCB は Common.hlsli の震えの欄と同じ 64 byte");

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
            TremorCB tremor{};
            float opacity = 1.0f; // 不透明度 0..1 (player.ps / unlit.ps で Alpha ブレンド時のみ有効)
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };
        static_assert(sizeof(FrameCB) == 288, "FrameCB size は standard.vs と完全一致 (288 byte)");
        static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");
    } // namespace Gfx
} // namespace NS
