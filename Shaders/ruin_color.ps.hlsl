#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 n = normalize(input.worldNormal);
    float light = dot(n, -normalize(lightDir));
    float shade = 0.69 + 0.23 * smoothstep(-0.2, 0.08, light) + 0.1 * smoothstep(0.55, 0.8, light);
    float3 tint = lerp(float3(0.63, 0.74, 0.82), float3(1.0, 0.98, 0.88), saturate(light + 0.4));
    return float4(ToDisplay(baseColor * shade * tint), g_opacity);
}
