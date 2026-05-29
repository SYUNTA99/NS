// Player / 単一テクスチャ用 PS。 標準の Texture2D diffuse をそのまま sampling して
// baseColor x (ambient + N.L * sun) のテーマ駆動ライティングを掛ける。
// Block 描画は instanced.vs.hlsl + standard.ps.hlsl (Texture2DArray) ペアに統一したため、
// 単一 mesh の Player などはこちらを使う。 FrameCB / Sampler の構造は standard.ps.hlsl と完全一致。

cbuffer FrameCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float3 lightDir;
    float  pad0;
    float3 baseColor;
    float  pad1;
    float3 g_lightColor;
    float  pad2;
    float3 g_ambientColor;
    float  pad3;
};

Texture2D    diffuse : register(t0);
SamplerState samp    : register(s0);

struct PSIn
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
};

float4 PSMain(PSIn input) : SV_TARGET
{
    float3 albedo = diffuse.Sample(samp, input.uv).rgb;
    float3 n = normalize(input.worldNormal);
    float  ndl = saturate(dot(n, -normalize(lightDir)));
    float3 lit = albedo * baseColor * (g_ambientColor + ndl * g_lightColor);
    return float4(lit, 1.0);
}
