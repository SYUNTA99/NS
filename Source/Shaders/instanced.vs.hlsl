// Instanced VS。 Block を (mesh, material) bucket に集約し DrawIndexedInstanced 1 回で
// 描く際に使う。 per-vertex 入力 (slot0) と per-instance 入力 (slot1) を組合せ、
// per-instance の 4 つの float4 行から world 行列を再構築する。
// 既存 standard.vs.hlsl と同じ row_major + 行ベクトル流派 (mul(float4(pos,1), world))。
// INSTANCE_COLOR.w は Texture2DArray の slice index で、 PS に instSlice : SLICE として渡す。

cbuffer FrameCB : register(b0)
{
    row_major float4x4 world;     // instanced では未使用 (per-instance 行列を優先)
    row_major float4x4 viewProj;
    float3 lightDir;
    float  pad0;
    float3 baseColor;             // material からの全体色、 per-instance baseColor と乗算する
    float  pad1;
    float3 g_lightColor;          // テーマ駆動 sun color (PS 側で使う、 VS は素通し)
    float  pad2;
    float3 g_ambientColor;        // テーマ駆動 ambient (PS 側で使う、 VS は素通し)
    float  pad3;
};

struct VSIn
{
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD;
    float3 normal : NORMAL;
    // slot 1 (per-instance)。 INSTANCE_WORLD0..3 で float4x4 を 4 行に分解、 INSTANCE_COLOR は
    // 個体色 (xyz=tint multiplier) + Texture2DArray slice index (.w)。
    // BlockInstance struct (sizeof==80) と AlignedByteOffset 整合済。
    float4 wRow0  : INSTANCE_WORLD0;
    float4 wRow1  : INSTANCE_WORLD1;
    float4 wRow2  : INSTANCE_WORLD2;
    float4 wRow3  : INSTANCE_WORLD3;
    float4 instColor : INSTANCE_COLOR;
};

struct VSOut
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
    float  instSlice   : SLICE;
};

VSOut VSMain(VSIn input)
{
    // 4 row の float4 を 1 つの row_major float4x4 として再構築。
    float4x4 instanceWorld = float4x4(input.wRow0, input.wRow1, input.wRow2, input.wRow3);

    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0), instanceWorld);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    // 等スケール前提なので 3x3 をそのまま掛ける。 非等スケール導入時に逆転置へ。
    output.worldNormal = normalize(mul(input.normal, (float3x3)instanceWorld));
    // INSTANCE_COLOR.w を slice index として PS に渡す。 補間器が intager にならないため float のまま流す。
    output.instSlice = input.instColor.w;
    return output;
}
