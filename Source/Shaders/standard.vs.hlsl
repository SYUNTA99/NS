// FrameCB は MeshVertex (POSITION + TEXCOORD + NORMAL) + Material::SetParams を前提とする。
// DirectXMath/SimpleMath が row-major LH のため、HLSL 側も row_major で揃える。
// `mul(float4(pos,1), world)` の行ベクトル流派で記述する。

cbuffer FrameCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float3 lightDir;
    float  pad0;
    float3 baseColor;
    float  pad1;
};

struct VSIn
{
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD;
    float3 normal : NORMAL;
};

struct VSOut
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0), world);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    // 等スケール前提なので世界行列の 3x3 をそのまま掛ける。非等スケール導入時に逆転置へ。
    output.worldNormal = normalize(mul(input.normal, (float3x3)world));
    return output;
}
