#include "Common.hlsli"

Texture2D paintedStone : register(t0);
SamplerState stoneSampler : register(s0);

cbuffer MaterialParams : register(b2)
{
    float worldProjection = 0.0;
    float textureScale = 0.18;
    float colorVariation = 0.12;
    float textureWeight = 0.65;
};

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 n = normalize(input.worldNormal);
    float2 uv = input.uv;
    float variation = frac(sin(floor(uv.x) * 43.17) * 7813.17);
    if (worldProjection > 0.5)
    {
        float3 across = abs(n.y) > 0.99 ? float3(1, 0, 0) : normalize(float3(-n.z, 0, n.x));
        uv = float2(dot(input.worldPos, across), dot(input.worldPos, cross(n, across))) * textureScale;
        variation = 0.5;
    }
    float3 painted = paintedStone.Sample(stoneSampler, uv).rgb;
    float3 albedo = baseColor * lerp(float3(1, 0.95, 0.84), painted, textureWeight);
    albedo *= 1.0 + (variation - 0.5) * colorVariation;
    if (worldProjection < 0.5)
    {
        float2 tile = frac(input.uv);
        float edge = min(min(tile.x, 1.0 - tile.x), min(tile.y, 1.0 - tile.y));
        albedo *= 0.86 + 0.14 * smoothstep(0.05, 0.12, edge);
    }
    float sun = dot(n, -normalize(lightDir));
    float light = 0.70 + 0.22 * smoothstep(-0.16, 0.1, sun) + 0.10 * smoothstep(0.55, 0.75, sun);
    float3 tint = lerp(float3(0.68, 0.75, 0.83), float3(1.0, 0.98, 0.91), smoothstep(-0.2, 0.45, sun));
    float3 color = ToDisplay(albedo * tint * light);
    float haze = smoothstep(100, 320, length(input.worldPos - g_cameraPosition)) * 0.32;
    return float4(lerp(color, float3(0.66, 0.79, 0.81), haze), g_opacity);
}
