// 水ブロック用 PS。 standard / player と同じテーマ駆動ライティングを掛けつつ、
// 出力 alpha を 1 未満に固定して半透明 (Alpha ブレンド) で描く。 player.ps.hlsl との差分は
// 末尾の alpha のみ。 placeholder の見た目用で、 将来は厚み/フレネル等に置換予定。

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

// 半透明度。0=完全透過 / 1=不透明。placeholder のため固定値
static const float kWaterAlpha = 0.45;

float4 PSMain(PSIn input) : SV_TARGET
{
    float3 albedo = diffuse.Sample(samp, input.uv).rgb;
    float3 n = normalize(input.worldNormal);
    float  ndl = saturate(dot(n, -normalize(lightDir)));
    float3 lit = albedo * baseColor * (g_ambientColor + ndl * g_lightColor);
    return float4(lit, kWaterAlpha);
}
