#ifndef NS_SURFACE_HOOKS_HLSLI
#define NS_SURFACE_HOOKS_HLSLI

// Recoilot の物ごとの見た目。 NSlib の Shaders/SurfaceHooks.hlsli と同じ関数を書き、 Game がこのフォルダを先に探させる
// 名前の無い物ごとの値 g_objectValues は、 衝撃の震え (GL::Level::BodyTremor::DrawValues) の並び
//   [0] 衝突点の world の位置からのずれ (m) と振れ幅 (m)
//   [1] 画面の右の向きと、 震え始めからの経過フレーム数
//   [2] 画面の上の向きと、 衝突点から 1 m あたりの遅れのフレーム数
//   [3].x 1 か所が震えるフレーム数
// 名前の無いシーンの値 (SceneValue で読む) は、 床の波 (GL::Level::HitScreenDirector) の並び
//   [0] 輪の中心のワールド X 座標・中心のワールド Z 座標・半径 (m)・強さ (0 以下で出さない)

// 衝撃の震えで世界の位置へ足すずれ。 衝突点から遠い所ほど遅れて震え始め、 1 か所は震えるフレーム数で弱まって止まる
// 経過の小数は距離から来るので、 ほとんど動かない帯が衝突点から裏へ走り、 波に見える
float3 SurfaceVertexOffset(float3 worldPos)
{
    float amplitude = g_objectValues[0].w;
    float ringFrames = g_objectValues[3].x;
    if (amplitude <= 0.0 || ringFrames <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    // 衝突点は描く形と一緒に動く。 world 行列の位置からのずれで持つ
    float3 contact = world[3].xyz + g_objectValues[0].xyz;
    float t = g_objectValues[1].w - distance(worldPos, contact) * g_objectValues[2].w;
    if (t < 0.0 || t >= ringFrames)
    {
        return float3(0.0, 0.0, 0.0);
    }
    // 1 フレームに半周する。 横は cos で始まりのフレームから振れ幅いっぱい、 縦はその半分の速さ
    const float k_Pi = 3.14159265;
    float envelope = 1.0 - t / ringFrames;
    return (g_objectValues[1].xyz * cos(k_Pi * t) + g_objectValues[2].xyz * sin(k_Pi * t * 0.5)) * amplitude * envelope;
}

// 床の波の輪の半分の幅 (m)。 真後ろのカメラから床を斜めに見下ろした時に、 輪が線ではなく帯に見える幅
static const float k_GroundWaveHalfWidth = 0.7;

// 床の波の輪の中の位置。 輪の真ん中が 0、 内の縁が -1、 外の縁が +1。 輪の外か上を向かない面は 2 を返す
// 床は頂点の少ない箱なので形は曲げず、 光と面の向きだけで、 波が床を走って見せる
float GroundWaveSlot(float3 worldPos, float3 worldNormal, out float3 outward)
{
    outward = float3(0.0, 0.0, 0.0);
    if (SceneValue(0).w <= 0.0 || worldNormal.y < 0.5)
    {
        return 2.0;
    }
    float2 fromCenter = worldPos.xz - SceneValue(0).xy;
    float distanceFromCenter = length(fromCenter);
    float slot = (distanceFromCenter - SceneValue(0).z) / k_GroundWaveHalfWidth;
    if (abs(slot) >= 1.0)
    {
        return 2.0;
    }
    float2 direction = fromCenter / max(distanceFromCenter, 1.0e-4);
    outward = float3(direction.x, 0.0, direction.y);
    return slot;
}

// 床の波の盛り上がりの斜面の向き。 内の斜面は中心へ、 外の斜面は外へ倒す。 光の当たり方が輪の前後で入れ替わる
float3 SurfaceNormal(float3 worldPos, float3 worldNormal)
{
    float3 outward;
    float slot = GroundWaveSlot(worldPos, worldNormal, outward);
    if (slot >= 1.0)
    {
        return worldNormal;
    }
    const float k_Pi = 3.14159265;
    return normalize(worldNormal + outward * sin(k_Pi * slot) * SceneValue(0).w);
}

// 床の波の頂に足す光。 輪の真ん中が一番明るく、 縁で 0
float3 SurfaceAddedLight(float3 worldPos, float3 worldNormal)
{
    float3 outward;
    float slot = GroundWaveSlot(worldPos, worldNormal, outward);
    if (slot >= 1.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float crest = 1.0 - slot * slot;
    return crest * crest * SceneValue(0).w * 0.6 * g_lightColor;
}

#endif
