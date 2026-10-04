#ifndef NS_COMMON_HLSLI
#define NS_COMMON_HLSLI

// 全 mesh 描画が共有する毎フレーム定数。 C++ の FrameCB と 1 対 1 に対応する 272 byte
// row-major LH に揃え mul(float4(pos,1), world) の行ベクトル流派で使う
cbuffer FrameCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float3 lightDir;
    float  g_groundWaveCenterX;
    float3 baseColor;
    float  g_groundWaveCenterZ;
    float3 g_lightColor;
    float  g_groundWaveRadius;
    float3 g_ambientColor;
    float  g_groundWaveStrength;
    float3 g_groundColor;
    float  g_exposure;
    // 物の震え。 C++ の TremorCB と同じ並び
    float3 g_tremorContactOffset;
    float  g_tremorAmplitude;
    float3 g_tremorRight;
    float  g_tremorElapsedFrames;
    float3 g_tremorUp;
    float  g_tremorFramesPerMeter;
    float  g_tremorRingFrames;
    float3 g_tremorPad;
};

// 物の震えで世界の位置へ足すずれ。 衝突点から遠い所ほど遅れて震え始め、 1 か所は g_tremorRingFrames で弱まって止まる
// 経過の小数は距離から来るので、 ほとんど動かない帯が衝突点から裏へ走り、 波に見える
float3 TremorOffset(float3 worldPos)
{
    if (g_tremorAmplitude <= 0.0 || g_tremorRingFrames <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    // 衝突点は描く形と一緒に動く。 world 行列の位置からのずれで持つ
    float3 contact = world[3].xyz + g_tremorContactOffset;
    float t = g_tremorElapsedFrames - distance(worldPos, contact) * g_tremorFramesPerMeter;
    if (t < 0.0 || t >= g_tremorRingFrames)
    {
        return float3(0.0, 0.0, 0.0);
    }
    // 1 フレームに半周する。 横は cos で始まりのフレームから振れ幅いっぱい、 縦はその半分の速さ
    const float k_Pi = 3.14159265;
    float envelope = 1.0 - t / g_tremorRingFrames;
    return (g_tremorRight * cos(k_Pi * t) + g_tremorUp * sin(k_Pi * t * 0.5)) * g_tremorAmplitude * envelope;
}

// standard.vs / skinned.vs が出力し PS が受け取る補間子
struct SurfaceInterp
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
    float3 worldPos    : TEXCOORD1;
};

// 床の波の輪の半分の幅 (m)。 真後ろのカメラから床を斜めに見下ろした時に、 輪が線ではなく帯に見える幅
static const float k_GroundWaveHalfWidth = 0.7;

// 床の波の輪の中の位置。 輪の真ん中が 0、 内の縁が -1、 外の縁が +1。 輪の外か上を向かない面は 2 を返す
// 床は頂点の少ない箱なので形は曲げず、 光と面の向きだけで、 波が床を走って見せる
float GroundWaveSlot(float3 worldPos, float3 worldNormal, out float3 outward)
{
    outward = float3(0.0, 0.0, 0.0);
    if (g_groundWaveStrength <= 0.0 || worldNormal.y < 0.5)
    {
        return 2.0;
    }
    float2 fromCenter = worldPos.xz - float2(g_groundWaveCenterX, g_groundWaveCenterZ);
    float distanceFromCenter = length(fromCenter);
    float slot = (distanceFromCenter - g_groundWaveRadius) / k_GroundWaveHalfWidth;
    if (abs(slot) >= 1.0)
    {
        return 2.0;
    }
    float2 direction = fromCenter / max(distanceFromCenter, 1.0e-4);
    outward = float3(direction.x, 0.0, direction.y);
    return slot;
}

// 床の波の盛り上がりの斜面の向き。 内の斜面は中心へ、 外の斜面は外へ倒す。 光の当たり方が輪の前後で入れ替わる
float3 GroundWaveNormal(float3 worldPos, float3 worldNormal)
{
    float3 outward;
    float slot = GroundWaveSlot(worldPos, worldNormal, outward);
    if (slot >= 1.0)
    {
        return worldNormal;
    }
    const float k_Pi = 3.14159265;
    return normalize(worldNormal + outward * sin(k_Pi * slot) * g_groundWaveStrength);
}

// 床の波の頂に足す光の量。 輪の真ん中が一番明るく、 縁で 0
float GroundWaveGlow(float3 worldPos, float3 worldNormal)
{
    float3 outward;
    float slot = GroundWaveSlot(worldPos, worldNormal, outward);
    if (slot >= 1.0)
    {
        return 0.0;
    }
    float crest = 1.0 - slot * slot;
    return crest * crest * g_groundWaveStrength * 0.6;
}

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
