// 単色 unlit PS。 lighting を掛けず baseColor をそのまま出力する
// テクスチャを sampling しないので texture を持たない .mat でも黒くならない

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    return float4(ToDisplay(baseColor), 1.0);
}
