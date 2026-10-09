// 出来上がった絵の歪みの輪の所を、 輪の内側の絵を引いて外へ押し出したように見せる。 輪の真ん中が一番強く、 縁で 0
// 歪めない画素は元の通り 1 画素ずつ読み、 絵を変えない
// 頂点は fade.vs の全画面三角形で、 画素の位置から UV を出す

cbuffer ScreenDistortionCB : register(b0)
{
    float4 g_ring;          // 歪みの輪の中心の x・y、 半径、 押し (書く絵の画素)。 押しが 0 以下なら歪めない
    float  g_ringHalfWidth; // 歪みの輪の半分の幅 (書く絵の画素)
    float  g_keepBody;      // 0 より大きいなら、 体の型が 1 の画素を押さない
    float2 g_padding;
};

Texture2D g_scene : register(t0);
Texture2D g_bodyMask : register(t1); // 体の型。 差していなければ全部 0 として読む
SamplerState g_linear : register(s0);

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    float3 original = g_scene.Load(int3(position.xy, 0)).rgb;
    if (g_keepBody > 0.0 && g_bodyMask.Load(int3(position.xy, 0)).r > 0.5)
    {
        return float4(original, 1.0);
    }
    float2 pixel = position.xy;
    if (g_ring.w > 0.0 && g_ringHalfWidth > 0.0)
    {
        float2 fromCenter = pixel - g_ring.xy;
        float distanceFromCenter = length(fromCenter);
        float slot = (distanceFromCenter - g_ring.z) / g_ringHalfWidth;
        if (abs(slot) < 1.0 && distanceFromCenter > 1.0e-3)
        {
            float crest = 1.0 - slot * slot;
            pixel -= fromCenter / distanceFromCenter * g_ring.w * crest * crest;
            float2 size;
            g_scene.GetDimensions(size.x, size.y);
            return float4(g_scene.Sample(g_linear, pixel / size).rgb, 1.0);
        }
    }
    return float4(original, 1.0);
}
