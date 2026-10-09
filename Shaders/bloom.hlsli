#ifndef BLOOM_HLSLI
#define BLOOM_HLSLI

// 光のにじみの全画面の段が共有する定数と拾い方
// 頂点は fade.vs の全画面三角形で、画素の位置から UV を出す

cbuffer BloomCB : register(b0)
{
    float2 g_sourceTexel;      // 読む絵の 1 画素の UV の幅と高さ
    float2 g_destinationTexel; // 書く絵の 1 画素の UV の幅と高さ
    float  g_threshold;        // これを超えた分だけをにじませる
    float  g_intensity;        // にじみを足す強さ
    float2 g_padding;
};

Texture2D g_source : register(t0);
SamplerState g_linear : register(s0);

float2 DestinationUv(float4 position)
{
    return position.xy * g_destinationTexel;
}

// 3×3 の山形 (真ん中 4・辺 2・角 1 を 16 で割る)。読む絵の 1 画素ずつ離して拾い、段の四角い粗さを均す
float3 SampleTent(float2 uv)
{
    float2 t = g_sourceTexel;
    float3 sum = g_source.Sample(g_linear, uv).rgb * 4.0;
    sum += g_source.Sample(g_linear, uv + float2(-t.x, 0.0)).rgb * 2.0;
    sum += g_source.Sample(g_linear, uv + float2(t.x, 0.0)).rgb * 2.0;
    sum += g_source.Sample(g_linear, uv + float2(0.0, -t.y)).rgb * 2.0;
    sum += g_source.Sample(g_linear, uv + float2(0.0, t.y)).rgb * 2.0;
    sum += g_source.Sample(g_linear, uv + float2(-t.x, -t.y)).rgb;
    sum += g_source.Sample(g_linear, uv + float2(t.x, -t.y)).rgb;
    sum += g_source.Sample(g_linear, uv + float2(-t.x, t.y)).rgb;
    sum += g_source.Sample(g_linear, uv + float2(t.x, t.y)).rgb;
    return sum / 16.0;
}

#endif
