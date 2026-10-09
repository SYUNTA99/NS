// 半透明の PS。 standard / player と同じ平行光を掛け、 出力 alpha に描く 1 回ごとの不透明度 g_opacity を出す
// 共有の water の material が使い、 Model が残像の写しを描く

#include "Common.hlsli"

Texture2D    diffuse : register(t0);
SamplerState samp    : register(s0);

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 albedo = diffuse.Sample(samp, input.uv).rgb;
    float3 lit = albedo * baseColor * DirectionalLight(input.worldNormal);
    return float4(ToDisplay(lit), saturate(g_opacity));
}
