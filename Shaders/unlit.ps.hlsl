// 単色 unlit PS。 lighting を掛けず baseColor をそのまま出力する (flat color material 用)。
// テクスチャを sampling しないので texture を持たない .mat でも黒くならない。
// cbuffer / 入力構造は standard.vs.hlsl の出力と一致させる。

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

struct PSIn
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
};

float4 PSMain(PSIn input) : SV_TARGET
{
    return float4(baseColor, 1.0);
}
