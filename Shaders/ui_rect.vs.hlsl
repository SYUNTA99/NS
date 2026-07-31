// UI 矩形の VS。 頂点バッファを使わず SV_VertexID から矩形 (三角形 2 枚) を生成する
// 矩形は CPU 側で clip 空間へ変換済みの値を cbuffer で受け取り、 Draw(6) で 1 枚描く

cbuffer UiRectCB : register(b0)
{
    float4 g_rect;  // clip 空間の左上 x, y と幅, 高さ (高さは画面下方向の量)
    float4 g_color;
};

struct VSOut
{
    float4 pos : SV_Position;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    static const float2 k_Corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 corner = k_Corners[vertexId];

    VSOut output;
    // clip 空間は y が上向きなので、 下方向の高さは引き算で進める
    output.pos = float4(g_rect.x + corner.x * g_rect.z, g_rect.y - corner.y * g_rect.w, 0.0, 1.0);
    return output;
}
