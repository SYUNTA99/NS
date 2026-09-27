// 1 段小さい絵を山形で拾って広げ、書き込み先の段へ足す。足すのは描画装置の合成で行う
// 小さい段から順に足すので、大きい段ほど細かいにじみと広いにじみを両方持つ

#include "bloom.hlsli"

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    return float4(SampleTent(DestinationUv(position)), 1.0);
}
