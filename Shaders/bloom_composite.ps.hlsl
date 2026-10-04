// 世界の絵ににじみを足し、画面へ出せる 1 までに丸める
// 足す強さが 0 の時は段を作り直さないので読まない。前に残った値が無限大なら 0 倍しても非数になる

#include "bloom.hlsli"

Texture2D g_scene : register(t1);

// 歪みの輪の所では、 輪の内側の絵を引いて外へ押し出したように見せる。 輪の真ん中が一番強く、 縁で 0
// 歪めない画素は元の通り 1 画素ずつ読み、 絵を変えない
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    float2 pixel = position.xy;
    bool distorted = false;
    if (g_ring.w > 0.0 && g_ringHalfWidth > 0.0)
    {
        float2 fromCenter = pixel - g_ring.xy;
        float distanceFromCenter = length(fromCenter);
        float slot = (distanceFromCenter - g_ring.z) / g_ringHalfWidth;
        if (abs(slot) < 1.0 && distanceFromCenter > 1.0e-3)
        {
            float crest = 1.0 - slot * slot;
            pixel -= fromCenter / distanceFromCenter * g_ring.w * crest * crest;
            distorted = true;
        }
    }
    float2 uv = pixel * g_destinationTexel;
    float3 color;
    if (distorted)
    {
        color = g_scene.Sample(g_linear, uv).rgb;
    }
    else
    {
        color = g_scene.Load(int3(position.xy, 0)).rgb;
    }
    if (g_intensity > 0.0)
    {
        color += SampleTent(uv) * g_intensity;
    }
    return float4(saturate(color), 1.0);
}
