// 世界の絵ににじみを足し、画面へ出せる 1 までに丸める
// 足す強さが 0 の時は段を作り直さないので読まない。前に残った値が無限大なら 0 倍しても非数になる

#include "bloom.hlsli"

Texture2D g_scene : register(t1);

// 元の絵は 1 画素ずつ読み、 にじみを足さない画素の絵を変えない
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    float2 uv = DestinationUv(position);
    float3 color = g_scene.Load(int3(position.xy, 0)).rgb;
    if (g_intensity > 0.0)
    {
        color += SampleTent(uv) * g_intensity;
    }
    return float4(saturate(color), 1.0);
}
