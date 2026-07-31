// Skinned 頂点 VS。 BonePalette(b1) の skinning 行列を weights で線形ブレンドし
// model 空間で変形してから world / viewProj を掛ける
// skin = Σ wᵢ * bones[jointᵢ] の先合成は CPU 参照の Σ wᵢ * (pos * palette) と線形性で一致する

#include "Common.hlsli"

cbuffer BonePalette : register(b1)
{
    row_major float4x4 bones[128];
};

struct VSIn
{
    float3 pos     : POSITION;
    float2 uv      : TEXCOORD;
    float3 normal  : NORMAL;
    uint4  joints  : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

SurfaceInterp VSMain(VSIn input)
{
    float4x4 skin = input.weights.x * bones[input.joints.x] +
                    input.weights.y * bones[input.joints.y] +
                    input.weights.z * bones[input.joints.z] +
                    input.weights.w * bones[input.joints.w];

    float4 skinnedPos    = mul(float4(input.pos, 1.0), skin);
    float3 skinnedNormal = mul(input.normal, (float3x3)skin);

    SurfaceInterp output;
    float4 worldPos = mul(skinnedPos, world);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    // 等スケール前提で skin / world の 3x3 をそのまま掛ける、 非等スケール導入時は逆転置へ
    output.worldNormal = normalize(mul(skinnedNormal, (float3x3)world));
    return output;
}
