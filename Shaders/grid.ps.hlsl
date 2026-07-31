// 手続き grid PS。 uv からマス目の境界線を生成し、 線部分を暗く・ 面部分を baseColor で塗って平行光を掛ける
// テクスチャ画像なしで模様を出す material 用

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    const float cells = 4.0;
    const float lineWidth = 0.05;

    float2 f = frac(input.uv * cells);
    float2 d = min(f, 1.0 - f);           // 各軸でセル境界までの距離
    float edge = min(d.x, d.y);
    float lineMask = 1.0 - smoothstep(0.0, lineWidth, edge); // 境界付近で 1
    float3 surface = lerp(baseColor, baseColor * 0.15, lineMask);

    // surface に baseColor を含むので DirectionalLight の項だけ掛ける
    float3 lit = surface * DirectionalLight(input.worldNormal);
    return float4(ToDisplay(lit), 1.0);
}
