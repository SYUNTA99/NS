#include "Common.hlsli"
#include "water.hlsli"

struct VSIn
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

WaterInterp VSMain(VSIn input)
{
    WaterInterp output;
    float4 position = mul(float4(input.pos, 1.0), world);
    output.basePos = position.xz;
    float2 offset;
    float height;
    float2 slope;
    WaterSwell(position.xz, g_sceneTimeSeconds, offset, height, slope);
    position.xz += offset;
    position.y += height;
    output.worldPos = position.xyz;
    output.pos = mul(position, viewProj);
    return output;
}
