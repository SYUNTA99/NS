// PS は diffuse テクスチャ × baseColor × (ambient + N.L * sun) のテーマ駆動ライティング。
// FrameCB のレイアウトは VS と完全一致。Sampler は CommonStates::LinearWrap が s0 に bind 済前提。
// g_lightColor / g_ambientColor は ThemeRegistry::Get(level.themeId) 由来で C++ 側から毎フレーム流し込む。

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
