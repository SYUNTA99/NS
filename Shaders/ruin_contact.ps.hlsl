#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float2 p = input.uv * 2.0 - 1.0;
    float falloff = saturate(1.0 - dot(p, p));
    return float4(baseColor, falloff * falloff * 0.32 * g_opacity);
}
