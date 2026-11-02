// UI の線の VS。 頂点バッファを使わず SV_VertexID から線の四角 (三角形 2 枚) を生成する
// 四隅は CPU 側で clip 空間へ変換済みの値を cbuffer で受け取り、 Draw(6) で 1 本描く。 斜めの線も描ける

cbuffer UiLineCB : register(b0)
{
    float4 g_cornersA; // 始まりの片側の x と y、 始まりの反対側の x と y
    float4 g_cornersB; // 終わりの片側の x と y、 終わりの反対側の x と y
    float4 g_color;
};

struct VSOut
{
    float4 pos : SV_Position;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 四隅の番号。 0 と 1 が始まり、 2 と 3 が終わり
    static const uint k_Corners[6] = { 0, 2, 1, 2, 3, 1 };
    const float2 corners[4] = { g_cornersA.xy, g_cornersA.zw, g_cornersB.xy, g_cornersB.zw };

    VSOut output;
    output.pos = float4(corners[k_Corners[vertexId]], 0.0, 1.0);
    return output;
}
