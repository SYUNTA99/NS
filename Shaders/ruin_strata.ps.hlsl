#include "Common.hlsli"

Texture2D paintedStone : register(t0);
Texture2D rockOcclusion : register(t1);
SamplerState stoneSampler : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 n = normalize(input.worldNormal);
    float3 weights = pow(abs(n), 4.0);
    weights /= max(weights.x + weights.y + weights.z, 0.001);
    float3 p = input.worldPos * 0.10;
    float3 paint = paintedStone.Sample(stoneSampler, p.yz).rgb * weights.x
                 + paintedStone.Sample(stoneSampler, p.xz).rgb * weights.y
                 + paintedStone.Sample(stoneSampler, p.xy).rgb * weights.z;
    float sun = saturate(dot(n, -normalize(lightDir)));
    float3 tint = lerp(float3(0.72, 0.77, 0.88), float3(1.04, 1.00, 0.91), smoothstep(0.0, 0.7, sun));
    float shade = 0.63 + 0.23 * smoothstep(0.02, 0.18, sun) + 0.14 * smoothstep(0.55, 0.75, sun);
    float variation = 0.95 + 0.05 * sin(input.worldPos.x * 0.17 + input.worldPos.y * 0.29);
    float ao = input.uv.x < -1.5 ? input.uv.y : (input.uv.x < 0 ? 0.9 : rockOcclusion.Sample(stoneSampler, input.uv).r);
    float occlusion = lerp(0.57, 1.0, smoothstep(0.05, 0.95, ao));
    float3 albedo = lerp(baseColor, paint, 0.62);
    float3 color = ToDisplay(albedo * tint * shade * variation * occlusion);
    float haze = smoothstep(110, 340, length(input.worldPos - g_cameraPosition)) * 0.28;
    return float4(lerp(color, float3(0.66, 0.78, 0.79), haze), 1);
}
