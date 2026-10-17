// 単色 unlit PS。 lighting を掛けず baseColor をそのまま出力する
// テクスチャを sampling しないので texture を持たない .mat でも黒くならない
// 出力 alpha は描く 1 回ごとの不透明度 g_opacity。 混ぜ方が Alpha の描画 (残像の写し) だけが効かせる

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    return float4(ToDisplay(baseColor), saturate(g_opacity));
}
