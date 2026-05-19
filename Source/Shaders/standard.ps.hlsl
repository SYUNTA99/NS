// PS は diffuse テクスチャ × baseColor × ランバート系 (0.3 ambient + 0.7 N.L) の最小ライティング。
// FrameCB のレイアウトは VS と完全一致。Sampler は CommonStates::LinearWrap が s0 に bind 済前提。

cbuffer FrameCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float3 lightDir;
    float  pad0;
    float3 baseColor;
    float  pad1;
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
    float3 lit = albedo * baseColor * (0.3 + 0.7 * ndl);
    return float4(lit, 1.0);
}
