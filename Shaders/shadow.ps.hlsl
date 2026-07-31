// 接地シャドウ用 PS。 テクスチャを使わず UV 中心からの距離で放射状の柔らかい影を手続き生成する
// 色は黒固定。 baseColor.x を高さフェードのアルファ係数として受け取り乗算する

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    // UV 中心 (0.5,0.5) からの距離で 1(中心)→0(外周)、 二乗で縁を柔らかくする
    float2 d = input.uv - 0.5;
    float  r = saturate(1.0 - length(d) * 2.0);
    float  a = r * r * baseColor.x; // baseColor.x = 高さフェードのアルファ
    return float4(0.0, 0.0, 0.0, a);
}
