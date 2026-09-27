// 閾値を超えた分だけを残す。同じ大きさの絵へ書くので画素をそのまま読む
// 縮める前に引く。縮めながら引くと、1 画素だけ明るい所が周りの暗さと平均されて閾値の下へ沈む

#include "bloom.hlsli"

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    float3 color = g_source.Load(int3(position.xy, 0)).rgb;
    return float4(max(color - g_threshold, 0.0), 1.0);
}
