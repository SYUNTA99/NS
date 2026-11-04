#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 n = normalize(input.worldNormal);
    float sun = saturate(dot(n, -normalize(lightDir)));
    float bark = 0.88 + 0.10 * sin(input.uv.x * 40.0 + sin(input.worldPos.y * 2.5));
    float grooves = smoothstep(0.79, 0.95, sin(input.uv.x * 62.83 + input.worldPos.y * 0.23));
    float shade = 0.64 + 0.46 * sun;
    float3 tint = lerp(float3(0.78, 0.84, 0.86), float3(1.12, 0.99, 0.79), sun);
    return float4(ToDisplay(baseColor * tint * shade * (bark - grooves * 0.13)), 1);
}
