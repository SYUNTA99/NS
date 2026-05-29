// Block 用 PS。 Texture2DArray g_BlockTextures から VS が流した instSlice : SLICE で
// 1 slice を選んで sampling し、 baseColor × (ambient + N.L * sun) のテーマ駆動ライティングを行う。
// FrameCB のレイアウトは VS と完全一致。 Sampler は CommonStates::LinearWrap が s0 に bind 済前提。
// g_lightColor / g_ambientColor は ThemeRegistry::Get(level.themeId) 由来で C++ 側から毎フレーム流し込む。
// VS 側で INSTANCE_COLOR.w → instSlice : SLICE を渡すので、 本 PS は必ず instanced.vs.hlsl とペアで使う。
// Player 等の単一 Texture2D 描画は player.ps.hlsl 側を使う (本 PS は block 専用に統一)。

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

Texture2DArray g_BlockTextures : register(t0);
SamplerState   samp            : register(s0);

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
    float3 n = normalize(input.worldNormal);
    float  ndl = saturate(dot(n, -normalize(lightDir)));
    float3 lit = albedo * baseColor * (g_ambientColor + ndl * g_lightColor);
    return float4(lit, 1.0);
}
