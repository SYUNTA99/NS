#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 n = normalize(input.worldNormal);
    float2 p = input.worldPos.xz;
    float patches = sin(p.x * 0.47 + sin(p.y * 0.31)) * sin(p.y * 0.62 + p.x * 0.19);
    float strokes = sin(p.x * 7.4 + sin(p.y * 4.1)) * sin(p.y * 6.3);
    float sun = saturate(dot(n, -normalize(lightDir)));
    float3 color = baseColor * (0.87 + patches * 0.09 + strokes * 0.035);
    color *= lerp(float3(0.53, 0.70, 0.64), float3(1.12, 1.06, 0.77), sun);
    return float4(ToDisplay(color * (0.65 + sun * 0.4)), 1);
}
