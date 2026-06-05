// Skybox VS。 単位 cube の頂点を `pos.xyww` swizzle で far plane (depth=1) に張り付ける。
// camera の translation 成分を抜いた viewProj が前提なので world matrix は不要 (identity 相当)。
// 入力レイアウトは StaticVertex (POSITION + TEXCOORD + NORMAL) と共有し、 既存 Mesh::StandardInputLayout
// をそのまま流用できる構造になっている。 cubemap サンプリング方向はローカル position をそのまま渡す。

cbuffer SkyboxCB : register(b0)
{
    row_major float4x4 g_viewProj;
};

struct VSIn
{
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD;
    float3 normal : NORMAL;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 clip = mul(float4(input.pos, 1.0), g_viewProj);
    // z = w に強制することで NDC z = 1 (far) に張り付き、 LESS_EQUAL で通過する。
    output.pos = clip.xyww;
    // local position は cube の中心から各面への方向ベクトル = cubemap sampling dir。
    output.dir = input.pos;
    return output;
}
