#include "Common.hlsli"

Texture2D canopyTexture : register(t0);
SamplerState canopySampler : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 n = normalize(input.worldNormal);
    float3 weights = pow(abs(n), 4.0);
    weights /= max(weights.x + weights.y + weights.z, 0.001);
    float3 p = input.worldPos * 0.16;
    float3 painted = canopyTexture.Sample(canopySampler, p.yz).rgb * weights.x
                   + canopyTexture.Sample(canopySampler, p.xz).rgb * weights.y
                   + canopyTexture.Sample(canopySampler, p.xy).rgb * weights.z;
    float light = dot(n, -normalize(lightDir));
    painted = lerp(float3(0.30, 0.43, 0.16), painted, 0.48);
    float shade = 0.68 + 0.32 * smoothstep(-0.25, 0.55, light);
    float3 color = ToDisplay(painted * baseColor * shade);
    float haze = smoothstep(100, 320, length(input.worldPos - g_cameraPosition)) * 0.25;
    return float4(lerp(color, float3(0.62, 0.77, 0.77), haze), g_opacity);
}
