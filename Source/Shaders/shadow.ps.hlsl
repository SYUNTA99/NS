// 接地シャドウ用 PS。テクスチャを使わず UV 中心からの距離で放射状の柔らかい影を手続き生成する。
// 色は黒固定。baseColor.x を高さフェードのアルファ係数として受け取り乗算する (ShadowComponent が詰める)。
// VS は standard.vs を流用するため FrameCB レイアウトは standard / player と完全一致 (192 byte)。

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
    // UV 中心 (0.5,0.5) からの距離で 1(中心)→0(外周)。二乗で縁を柔らかくする
    float2 d = input.uv - 0.5;
    float  r = saturate(1.0 - length(d) * 2.0);
    float  a = r * r * baseColor.x; // baseColor.x = 高さフェードのアルファ
    return float4(0.0, 0.0, 0.0, a);
}
