// 全画面フェード VS。 頂点バッファを使わず SV_VertexID から画面全体を覆う 1 枚の三角形を生成する。
// 入力レイアウト不要で、 0/1/2 の 3 頂点を Draw(3) で投げるだけで NDC 全域を覆える。

struct VSOut
{
    float4 pos : SV_Position;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
