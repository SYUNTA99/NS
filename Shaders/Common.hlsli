#ifndef NS_COMMON_HLSLI
#define NS_COMMON_HLSLI

// 全 mesh 描画が共有する毎フレーム定数。 C++ の Material::SetParams と 1 対 1 に対応する 192 byte
// row-major LH に揃え mul(float4(pos,1), world) の行ベクトル流派で使う
cbuffer FrameCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float3 lightDir;
    float  pad0;
    float3 baseColor;
    float  pad1;
    float3 g_lightColor;
    float  pad2;
    float3 g_ambientColor;
    float  pad3;
    float3 g_groundColor;
    float  g_exposure;
};

// standard.vs / skinned.vs が出力し PS が受け取る補間子
struct SurfaceInterp
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
};

// 上下 2 色の環境光に平行光を足して返す。 albedo と baseColor は呼び出し側で掛ける
float3 DirectionalLight(float3 worldNormal)
{
    float3 n = normalize(worldNormal);
    // 上を向く面には空の色、 下を向く面には地面の色を回す
    // 影側が一律の暗さにならず、 光が当たらない面でも向きの差が残って形を読み取れる
    float3 ambient = lerp(g_groundColor, g_ambientColor, n.y * 0.5 + 0.5);
    // 明暗の境を緩める。 生の N・L だと光からわずかに傾いただけで一気に落ちて足場の縁が潰れる
    // 二乗して戻さないと全体が持ち上がりすぎ、 光の向きが読めなくなる
    float  ndl = saturate(dot(n, -normalize(lightDir)) * 0.5 + 0.5);
    return ambient + ndl * ndl * g_lightColor;
}

// 露出を掛けた後に S 字で潰して画面へ出す色にする
// 素通しだと光が正面から当たる面が 1.0 で頭打ちになり、 明るさを足しても白く塗り潰れるだけで何も変わらない
// S 字は暗部を持ち上げ明部を寝かせるので、 全体を明るくしても飛ばず影の中の形も残る
// 曲線は光の量に対して引くものなので、 素材の色をいったん光の量へ直してから通して画面の値へ戻す
// 直さずに掛けると中間が持ち上がりすぎ、 色が抜けて眠い絵になる
float3 ToDisplay(float3 color)
{
    float3 x = pow(max(color, 0.0), 2.2) * g_exposure;
    x = saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
    return pow(x, 1.0 / 2.2);
}

#endif
