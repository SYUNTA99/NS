// 床用 PS。 市松をワールドの位置から計算で描き、 baseColor と平行光と床の波を掛ける
// 四角の長さはワールドの長さなので、 床の大きさを変えても四角は伸びない
// 出力 alpha は描く 1 回ごとの不透明度 g_opacity

#include "Common.hlsli"

// 材質の値。 .mat の params に名前で書く
cbuffer MaterialParams : register(b2)
{
    float tileLength = 3.125; // 四角 1 つの長さ (m)
};

// 暗い四角の明るさ。 明るい四角は 1。 前に床へ貼っていた cube_test.png の濃い灰と同じ値
static const float k_DarkTile = 64.0 / 255.0;

// 面に沿った 2 つの向きでの位置。 上の面も横の面も同じ長さの四角になる
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

// 四角の単位の位置での市松。 0 が暗い四角、 1 が明るい四角
// 1 ピクセルが覆う範囲の平均を取る。 遠くの細かい四角を点で拾うと模様がちらつく (前のテクスチャはミップマップで平均していた)
float Checker(float2 tile)
{
    float2 w = fwidth(tile) + 1.0e-4;
    float2 i = 2.0 * (abs(frac((tile - 0.5 * w) * 0.5) - 0.5) - abs(frac((tile + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    // 0 以下の長さで割らない
    float2 tile = TilePlane(input.worldPos, input.worldNormal) / max(tileLength, 1.0e-3);
    float3 albedo = lerp(k_DarkTile, 1.0, Checker(tile)).xxx;
    // 床の波は player.ps と同じ。 上を向く面だけ、 斜面の向きで明暗を付け、 頂へ光を足す
    float3 normal = GroundWaveNormal(input.worldPos, input.worldNormal);
    float3 lit = albedo * baseColor * DirectionalLight(normal) +
                 GroundWaveGlow(input.worldPos, input.worldNormal) * g_lightColor;
    return float4(ToDisplay(lit), saturate(g_opacity));
}
