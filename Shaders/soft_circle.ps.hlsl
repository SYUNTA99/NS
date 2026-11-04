// 透明の丸の PS。 テクスチャを使わず、 UV の中心からの距離で外へ薄くなる丸を出す。 接地の影と衝撃の跡が使う
// 色は baseColor、 中心の濃さは描く 1 回ごとの不透明度 g_opacity × strength

#include "Common.hlsli"

// 材質の値。 .mat の params に名前で書く
cbuffer MaterialParams : register(b2)
{
    float strength = 1.0; // 物ごとの g_opacity に掛ける濃さ。 木の影や接地の影のように .mat で決める物が使う
};

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    // UV 中心 (0.5,0.5) からの距離で 1(中心)→0(外周)、 二乗で縁を柔らかくする
    float2 d = input.uv - 0.5;
    float  r = saturate(1.0 - length(d) * 2.0);
    return float4(ToDisplay(baseColor), r * r * saturate(g_opacity) * strength);
}
