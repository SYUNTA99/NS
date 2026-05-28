// Skybox PS。 cubemap を方向ベクトルでサンプルしてそのまま出力する。
// sampler は LinearClamp (cubemap 境界で wrap させない)、 cubemap SRV は t0 固定。

TextureCube  g_Cubemap : register(t0);
SamplerState g_Sampler : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

float4 PSMain(PSIn input) : SV_Target
{
    float3 d = normalize(input.dir);
    return g_Cubemap.Sample(g_Sampler, d);
}
