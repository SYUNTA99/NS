// 読めないマテリアルと、マテリアルの無いファイルのモデルを描く PS
// 光も個体の色も掛けずにピンクだけを出し、壊れている事を一目で分かるようにする

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    return float4(ToDisplay(float3(1.0, 0.0, 1.0)), saturate(g_opacity));
}
