// 読む絵を半分の大きさへ縮める。13 点を線形で拾い、重なった 5 つの四角の平均にする
// 真ん中の四角が 0.5、四隅の四角が 0.125 ずつで、重みの合計は 1。光の量を増やしも減らしもしない
// 2×2 の平均で縮めると、動く細かい光が画素の境をまたぐたびに明滅する

#include "bloom.hlsli"

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    float2 uv = DestinationUv(position);
    float2 t = g_sourceTexel;

    float3 a = g_source.Sample(g_linear, uv + t * float2(-2.0, -2.0)).rgb;
    float3 b = g_source.Sample(g_linear, uv + t * float2(0.0, -2.0)).rgb;
    float3 c = g_source.Sample(g_linear, uv + t * float2(2.0, -2.0)).rgb;
    float3 d = g_source.Sample(g_linear, uv + t * float2(-2.0, 0.0)).rgb;
    float3 e = g_source.Sample(g_linear, uv).rgb;
    float3 f = g_source.Sample(g_linear, uv + t * float2(2.0, 0.0)).rgb;
    float3 g = g_source.Sample(g_linear, uv + t * float2(-2.0, 2.0)).rgb;
    float3 h = g_source.Sample(g_linear, uv + t * float2(0.0, 2.0)).rgb;
    float3 i = g_source.Sample(g_linear, uv + t * float2(2.0, 2.0)).rgb;
    float3 j = g_source.Sample(g_linear, uv + t * float2(-1.0, -1.0)).rgb;
    float3 k = g_source.Sample(g_linear, uv + t * float2(1.0, -1.0)).rgb;
    float3 l = g_source.Sample(g_linear, uv + t * float2(-1.0, 1.0)).rgb;
    float3 m = g_source.Sample(g_linear, uv + t * float2(1.0, 1.0)).rgb;

    float3 sum = e * 0.125;
    sum += (a + c + g + i) * 0.03125;
    sum += (b + d + f + h) * 0.0625;
    sum += (j + k + l + m) * 0.125;
    return float4(sum, 1.0);
}
