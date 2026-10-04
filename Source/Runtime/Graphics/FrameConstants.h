#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/RenderSettings.h"

namespace NS::Gfx
{
    //! @brief 物の震え。頂点のシェーダーが、衝突点からの距離の分だけ遅らせて、描く形を画面の平面の中でずらす
    //! @details 衝突点は world 行列の位置 + contactOffset。描く形と一緒に動き、描画の補間にも付いていく
    //! 頂点ごとに 経過 = elapsedFrames − 衝突点からの距離 × framesPerMeter を出す
    //! 経過が 0 以上 ringFrames 未満の間だけ、次のずれを世界の位置へ足す
    //! (right × cos(π × 経過) + up × sin(π × 経過 ÷ 2)) × amplitude × (1 − 経過 ÷ ringFrames)
    //! 1 フレームに半周するので、1 秒 60 フレームで画面の横に 1 秒 30 回、縦に 15 回
    //! 式は Shaders/Common.hlsli の TremorOffset が持つ。振れ幅が 0 の物は震えない
    struct alignas(16) TremorCB
    {
        NS::Core::Vector3 contactOffset{};         //!< 衝突点の、world 行列の位置からのずれ (世界の長さ、m)
        float amplitude = 0.0f;                    //!< 振れ幅 (m)。0 で震えない
        NS::Core::Vector3 right{1.0f, 0.0f, 0.0f}; //!< 画面の右の世界の向き。長さ 1
        float elapsedFrames = 0.0f;                //!< 震え始めてからのゲームのフレーム数。始まりのフレームが 0
        NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};    //!< 画面の上の世界の向き。長さ 1
        float framesPerMeter = 0.0f;               //!< 衝突点から 1 m 離れるごとに遅れて始まるフレーム数
        float ringFrames = 0.0f;                   //!< 1 か所が震えるフレーム数。0 以下は震えない
        float pad0 = 0.0f;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    };
    static_assert(sizeof(TremorCB) == 64, "TremorCB は Common.hlsli の震えの欄と同じ 64 byte");

    //! @brief 描画 1 回ごとの定数バッファ。standard.{vs,ps} と完全一致で sizeof=272、row_major LH
    //! @details world / viewProj のオブジェクト単位値に照明と床の波を束ねる。Material の内蔵 CB へ流す
    struct alignas(16) FrameCB
    {
        NS::Core::Matrix world{};
        NS::Core::Matrix viewProj{};
        // 照明の既定値はプロジェクト描画既定値の RenderSettings と共有し、値の二重管理を避ける
        NS::Core::Vector3 lightDir = NS::Gfx::RenderSettings{}.lightDir;
        float groundWaveCenterX = 0.0f; //!< 床の波の中心の世界の x。GroundWave と同じ意味
        NS::Core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        float groundWaveCenterZ = 0.0f; //!< 床の波の中心の世界の z
        NS::Core::Vector3 lightColor = NS::Gfx::RenderSettings{}.lightColor;
        float groundWaveRadius = 0.0f; //!< 床の波の輪の半径 (m)
        NS::Core::Vector3 ambientColor = NS::Gfx::RenderSettings{}.ambientColor;
        float groundWaveStrength = 0.0f; //!< 床の波の強さ。0 以下なら出さない
        NS::Core::Vector3 groundColor = NS::Gfx::RenderSettings{}.groundColor;
        float exposure = NS::Gfx::RenderSettings{}.exposure;
        TremorCB tremor{}; //!< 物の震え。頂点のシェーダーが読む。書かなければ震えない
    };
    static_assert(sizeof(FrameCB) == 272, "FrameCB size は standard.vs と完全一致 (272 byte)");
    static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");

} // namespace NS::Gfx
