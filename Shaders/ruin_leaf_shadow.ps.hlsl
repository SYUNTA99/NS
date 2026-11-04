#include "Common.hlsli"

Texture2D leafSpray : register(t0);
SamplerState leafSampler : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float alpha = leafSpray.Sample(leafSampler, input.uv).a;
    return float4(0.23, 0.28, 0.34, alpha * 0.115);
}
