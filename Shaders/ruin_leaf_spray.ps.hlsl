#include "Common.hlsli"

Texture2D leafSpray : register(t0);
SamplerState leafSampler : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float tone = floor((input.uv.x + 0.001) * 0.5);
    float2 uv = float2(input.uv.x - tone * 2.0, input.uv.y);
    float4 painted = leafSpray.Sample(leafSampler, uv);
    clip(painted.a - 0.45);
    float3 n = normalize(input.worldNormal);
    float sun = saturate(dot(n, -normalize(lightDir)) * 0.5 + 0.5);
    float3 tint = tone < 0.5 ? float3(0.72, 0.84, 0.76)
                : tone < 1.5 ? float3(1.12, 1.06, 0.78) : float3(0.89, 0.96, 0.83);
    float shade = 0.76 + 0.24 * sun;
    float3 color = painted.rgb * tint * shade;
    float haze = smoothstep(100, 320, length(input.worldPos - g_cameraPosition)) * 0.26;
    return float4(lerp(color, float3(0.62, 0.76, 0.75), haze), 1.0);
}
