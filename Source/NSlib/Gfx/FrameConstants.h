#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/RenderSettings.h"

namespace NS
{
    namespace Gfx
    {
        //! 衝突点からの距離に応じて遅れる頂点の震え。振れ幅 0 で無効
        //! contactOffset は描画姿勢に追従する。計算式は Shaders/Common.hlsli の TremorOffset
        struct alignas(16) TremorCB
        {
            Vector3 contactOffset{};         //!< 衝突点の、世界行列の位置からのずれ (世界の長さ、m)
            float amplitude = 0.0f;              //!< 振れ幅 (m)。0 で震えない
            Vector3 right{1.0f, 0.0f, 0.0f}; //!< 画面の右の世界の向き。長さ 1
            float elapsedFrames = 0.0f;          //!< 震え始めてからのゲームのフレーム数。始まりのフレームが 0
            Vector3 up{0.0f, 1.0f, 0.0f};    //!< 画面の上の世界の向き。長さ 1
            float framesPerMeter = 0.0f;         //!< 衝突点から 1 m 離れるごとに遅れて始まるフレーム数
            float ringFrames = 0.0f;             //!< 1 か所が震えるフレーム数。0 以下は震えない
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };
        static_assert(sizeof(TremorCB) == 64, "TremorCB は Common.hlsli の震えの欄と同じ 64 byte");

        //! @brief 描画 1 回ごとの定数バッファー。standard.{vs,ps} と配置が一致する 288 バイト、行優先の左手系
        //! @details world / viewProj と照明・床の波を、D3DMaterial の定数バッファーへ渡す
        struct alignas(16) FrameCB
        {
            Matrix world{};
            Matrix viewProj{};
            // 照明の既定値はプロジェクト描画既定値の RenderSettings と共有し、値の二重管理を避ける
            Vector3 lightDir = Gfx::RenderSettings{}.lightDir;
            float groundWaveCenterX = 0.0f; //!< 床の波の中心の世界の x。GroundWave と同じ意味
            Vector3 baseColor{1.0f, 1.0f, 1.0f};
            float groundWaveCenterZ = 0.0f; //!< 床の波の中心の世界の z
            Vector3 lightColor = Gfx::RenderSettings{}.lightColor;
            float groundWaveRadius = 0.0f; //!< 床の波の輪の半径 (m)
            Vector3 ambientColor = Gfx::RenderSettings{}.ambientColor;
            float groundWaveStrength = 0.0f; //!< 床の波の強さ。0 以下なら出さない
            Vector3 groundColor = Gfx::RenderSettings{}.groundColor;
            float exposure = Gfx::RenderSettings{}.exposure;
            TremorCB tremor{};    //!< 物の震え。頂点のシェーダーが読む。書かなければ震えない
            float opacity = 1.0f; //!< 半透明の材質が出す不透明度 0..1。読むのは water.ps だけ
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };
        static_assert(sizeof(FrameCB) == 288, "FrameCB size は standard.vs と完全一致 (288 byte)");
        static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");
    } // namespace Gfx
} // namespace NS
