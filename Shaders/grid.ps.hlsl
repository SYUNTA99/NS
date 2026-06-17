// 手続き grid PS。 uv からマス目の境界線を生成し、 線部分を暗く・ 面部分を baseColor で塗って
// directional ライティングを掛ける。 テクスチャ画像なしで模様を出す material 用。
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
    const float cells = 4.0;
    const float lineWidth = 0.05;

    float2 f = frac(input.uv * cells);
    float2 d = min(f, 1.0 - f);           // 各軸でセル境界までの距離
    float edge = min(d.x, d.y);
    float lineMask = 1.0 - smoothstep(0.0, lineWidth, edge); // 境界付近で 1
    float3 surface = lerp(baseColor, baseColor * 0.15, lineMask);

    float3 n = normalize(input.worldNormal);
    float ndl = saturate(dot(n, -normalize(lightDir)));
    float3 lit = surface * (g_ambientColor + ndl * g_lightColor);
    return float4(lit, 1.0);
}
