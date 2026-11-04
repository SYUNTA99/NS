#include "Common.hlsli"

cbuffer MaterialParams : register(b2)
{
    float surfaceKind = 0.0;
    float detailScale = 1.0;
    float detailStrength = 0.12;
};

float Hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 normal = GroundWaveNormal(input.worldPos, normalize(input.worldNormal));
    float light = dot(normal, -normalize(lightDir));
    float shade = 0.66 + 0.20 * smoothstep(-0.2, 0.12, light)
                      + 0.14 * smoothstep(0.55, 0.8, light);
    float3 across = abs(normal.y) > 0.99 ? float3(1, 0, 0) : normalize(float3(-normal.z, 0, normal.x));
    float3 along = cross(normal, across);
    float2 p = float2(dot(input.worldPos, across), dot(input.worldPos, along)) * detailScale;
    float row = floor(p.y / 1.65);
    float2 tile = float2(p.x / 2.4 + fmod(abs(row), 2.0) * 0.5, p.y / 1.65);
    float2 edge = min(frac(tile), 1.0 - frac(tile));
    float2 aa = max(fwidth(tile), 0.0001);
    float joint = 1.0 - min(smoothstep(0.008, 0.014 + aa.x, edge.x), smoothstep(0.01, 0.018 + aa.y, edge.y));
    float stone = Hash(floor(tile));
    float mottling = sin(p.x * 1.9 + sin(p.y * 1.4)) * sin(p.y * 2.8 + p.x * 0.7);
    float3 color = baseColor * (1.0 + (stone - 0.5) * detailStrength + mottling * detailStrength * 0.15);
    if (surfaceKind < 0.5)
    {
        color *= 1.0 - joint * 0.22;
    }
    if (surfaceKind > 1.5 && surfaceKind < 2.5)
    {
        float arrow = abs(input.uv.y - 0.28 - abs(input.uv.x - 0.5) * 0.65);
        float second = abs(input.uv.y - 0.65 - abs(input.uv.x - 0.5) * 0.65);
        float mark = 1.0 - smoothstep(0.025, 0.04 + fwidth(input.uv.y), min(arrow, second));
        mark *= smoothstep(0.12, 0.18, input.uv.x) * (1.0 - smoothstep(0.82, 0.88, input.uv.x));
        color = lerp(color, float3(1.0, 0.90, 0.63), mark * 0.85);
    }
    float3 shadowTint = lerp(float3(0.68, 0.79, 0.82), float3(1.0, 0.98, 0.90), saturate(light * 0.7 + normal.y * 0.25 + 0.45));
    float3 lit = color * shade * shadowTint + GroundWaveGlow(input.worldPos, normal) * 0.3;
    float depth = 0.1 * 550.0 / max(550.0 - input.pos.z * 549.9, 0.01);
    float haze = smoothstep(90.0, 270.0, depth) * 0.40;
    return float4(lerp(ToDisplay(lit), float3(0.66, 0.79, 0.77), haze), saturate(g_opacity));
}
