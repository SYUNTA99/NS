// StaticVertex (POSITION + TEXCOORD + NORMAL) を world / viewProj で変換する VS
// row-major LH に揃え mul(float4(pos,1), world) の行ベクトル流派で書く

#include "Common.hlsli"

struct VSIn
{
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD;
    float3 normal : NORMAL;
};

SurfaceInterp VSMain(VSIn input)
{
    SurfaceInterp output;
    float4 worldPos = mul(float4(input.pos, 1.0), world);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    // 等スケール前提なので世界行列の 3x3 をそのまま掛ける、 非等スケール導入時は逆転置へ
    output.worldNormal = normalize(mul(input.normal, (float3x3)world));
    return output;
}
