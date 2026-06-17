// Skinned 頂点 VS。 BonePalette(b1) の skinning 行列を weights で線形ブレンド (LBS) し、
// 頂点を model 空間で変形してから world / viewProj を掛ける
// FrameCB(b0) は standard.vs.hlsl と完全一致で、 PS は単一テクスチャの player.ps.hlsl とペアにする
// (VSOut が pos / uv / worldNormal で一致するため)
// DirectXMath / SimpleMath が row-major LH のため HLSL も row_major、 mul(vector, matrix) の行ベクトル流派
// skin = Σ wᵢ * bones[jointᵢ] と先に行列を合成する形は CPU 参照の Σ wᵢ * (pos * palette) と線形性で一致する

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

struct VSOut
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
};

VSOut VSMain(VSIn input)
{
    float4x4 skin = input.weights.x * bones[input.joints.x] +
                    input.weights.y * bones[input.joints.y] +
                    input.weights.z * bones[input.joints.z] +
                    input.weights.w * bones[input.joints.w];

    float4 skinnedPos    = mul(float4(input.pos, 1.0), skin);
    float3 skinnedNormal = mul(input.normal, (float3x3)skin);

    VSOut output;
    float4 worldPos = mul(skinnedPos, world);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    // 等スケール前提で skin / world の 3x3 をそのまま掛ける、 非等スケール導入時は逆転置へ
    output.worldNormal = normalize(mul(skinnedNormal, (float3x3)world));
    return output;
}
