// Player / 単一テクスチャ用 PS。 Texture2D diffuse を sampling し baseColor と平行光を掛ける
// 単一 mesh の Player などはこちらを使う

#include "Common.hlsli"

Texture2D    diffuse : register(t0);
SamplerState samp    : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 albedo = diffuse.Sample(samp, input.uv).rgb;
    // 床の波は上を向く面だけ、 斜面の向きで明暗を付け、 頂へ光を足す
    float3 normal = GroundWaveNormal(input.worldPos, input.worldNormal);
    float3 lit = albedo * baseColor * DirectionalLight(normal) +
                 GroundWaveGlow(input.worldPos, input.worldNormal) * g_lightColor;
    return float4(ToDisplay(lit), 1.0);
}
