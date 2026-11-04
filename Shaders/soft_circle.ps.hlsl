// 透明の丸の PS。 テクスチャを使わず、 UV の中心からの距離で外へ薄くなる丸を出す。 接地の影と衝撃の跡が使う
// 色は baseColor、 中心の濃さは描く 1 回ごとの不透明度 g_opacity

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    // UV 中心 (0.5,0.5) からの距離で 1(中心)→0(外周)、 二乗で縁を柔らかくする
    float2 d = input.uv - 0.5;
    float  r = saturate(1.0 - length(d) * 2.0);
    return float4(ToDisplay(baseColor), r * r * saturate(g_opacity));
}
