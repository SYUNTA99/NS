// Block 用 PS。 Texture2DArray から VS の instSlice で 1 slice を選んで sampling し baseColor と平行光を掛ける
// Sampler は s0 に bind 済前提。 instSlice を運ぶため instanced 系 VS とペアで使う
// Player 等の単一 Texture2D 描画は player.ps.hlsl を使う

#include "Common.hlsli"

Texture2DArray g_BlockTextures : register(t0);
SamplerState   samp            : register(s0);

// instSlice を持つため共通の SurfaceInterp ではなく専用の入力を使う
struct PSIn
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
    float  instSlice   : SLICE;
};

float4 PSMain(PSIn input) : SV_TARGET
{
    float3 albedo = g_BlockTextures.Sample(samp, float3(input.uv, input.instSlice)).rgb;
    float3 lit = albedo * baseColor * DirectionalLight(input.worldNormal);
    return float4(ToDisplay(lit), 1.0);
}
