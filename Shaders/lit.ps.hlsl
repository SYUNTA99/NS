// 光を受ける面の PS。 テクスチャと baseColor と平行光を掛ける。 模様の違いはテクスチャと .mat の値で出す
// 出力 alpha は描く 1 回ごとの不透明度 g_opacity。 混ぜ方が Alpha の描画 (残像の写し) だけが効かせる

#include "Common.hlsli"

Texture2D    diffuse : register(t0);
SamplerState samp    : register(s0);

// 材質の値。 .mat の params に名前で書く
cbuffer MaterialParams : register(b2)
{
    bool worldProjection = false; // uv の代わりに面に沿ったワールドの位置で貼る
    float textureScale = 1.0;     // uv かワールドの位置に掛ける。 ワールドの位置で貼る時は 1 m あたりのテクスチャの枚数
    float alphaCutoff = 0.0;      // テクスチャの alpha がこれより小さいピクセルは描かない
};

// 面に沿った 2 つの向きでの位置。 上の面も横の面も同じ大きさで貼れて、 面の大きさを変えても模様は伸びない
// 横と斜めの面は、 面に沿った水平の向きを法線から出す。 法線の一番大きい軸で分けると、 45 度に回した面で
// 2 つの軸の大きさが揃い、 ピクセルごとの誤差で選ぶ軸が入れ替わって点々が出た
float2 TilePlane(float3 worldPos, float3 worldNormal)
{
    float3 n = normalize(worldNormal);
    if (abs(n.y) > 0.999)
    {
        return worldPos.xz;
    }
    float3 across = normalize(float3(-n.z, 0.0, n.x));
    float3 along = cross(n, across);
    // 面を上る向きを正にする。 横の面では高さと同じ向きになる
    if (along.y < 0.0)
    {
        along = -along;
    }
    return float2(dot(worldPos, across), dot(worldPos, along));
}

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float2 uv = input.uv;
    if (worldProjection)
    {
        uv = TilePlane(input.worldPos, input.worldNormal);
    }
    float4 texel = diffuse.Sample(samp, uv * textureScale);
    if (texel.a < alphaCutoff)
    {
        discard;
    }
    // 床の波は上を向く面だけ、 斜面の向きで明暗を付け、 頂へ光を足す
    float3 normal = GroundWaveNormal(input.worldPos, input.worldNormal);
    float3 lit = texel.rgb * baseColor * DirectionalLight(normal) +
                 GroundWaveGlow(input.worldPos, input.worldNormal) * g_lightColor;
    return float4(ToDisplay(lit), saturate(g_opacity));
}
