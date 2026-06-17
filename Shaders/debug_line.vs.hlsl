// stub: pass-through。
cbuffer DebugCB : register(b0)
{
    row_major float4x4 viewProj;
};

struct VSIn
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

PSIn VSMain(VSIn IN)
{
    PSIn OUT;
    OUT.pos = mul(float4(IN.pos, 1.0), viewProj);
    OUT.color = IN.color;
    return OUT;
}
