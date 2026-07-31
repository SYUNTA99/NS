// Player / 単一テクスチャ用 PS。 Texture2D diffuse を sampling し baseColor と平行光を掛ける
// 単一 mesh の Player などはこちらを使う

#include "Common.hlsli"

Texture2D    diffuse : register(t0);
SamplerState samp    : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 albedo = diffuse.Sample(samp, input.uv).rgb;
    float3 lit = albedo * baseColor * DirectionalLight(input.worldNormal);
    return float4(ToDisplay(lit), 1.0);
}
