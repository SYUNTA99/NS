// 水ブロック用 PS。 standard / player と同じ平行光を掛けつつ出力 alpha を 1 未満に固定して半透明で描く
// placeholder の見た目用で将来は厚み / フレネル等に置換予定

#include "Common.hlsli"

Texture2D    diffuse : register(t0);
SamplerState samp    : register(s0);

// 半透明度。 0=完全透過 / 1=不透明。 placeholder のため固定値
static const float kWaterAlpha = 0.45;

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 albedo = diffuse.Sample(samp, input.uv).rgb;
    float3 lit = albedo * baseColor * DirectionalLight(input.worldNormal);
    return float4(ToDisplay(lit), kWaterAlpha);
}
