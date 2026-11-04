// 光を受けない単色の PS。 光を掛けず baseColor をそのまま出力する
// テクスチャを sampling しないので texture を持たない .mat でも黒くならない
// 出力 alpha は描く 1 回ごとの不透明度 g_opacity。 混ぜ方が Alpha の描画 (残像の写し) だけが効かせる

#include "Common.hlsli"

// 材質の値。 .mat の params に名前で書く
cbuffer MaterialParams : register(b2)
{
    float3 fixedColor = float3(1.0, 1.0, 1.0); // useFixedColor の時に出す色
    bool useFixedColor = false;                // アクターの色を無視する。 読めないマテリアルをピンクのままにする missing.mat が使う
};

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float3 color = baseColor;
    if (useFixedColor)
    {
        color = fixedColor;
    }
    return float4(ToDisplay(color), saturate(g_opacity));
}
