// 選んだ描く物の写る画素を 1 にする。 頂点は描く物の material の頂点シェーダが出す
// 世界の深度で隠れた画素はパイプラインの深度の比べで捨てるので、 ここでは色を出すだけ

#include "Common.hlsli"

float4 PSMain(SurfaceInterp input) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
