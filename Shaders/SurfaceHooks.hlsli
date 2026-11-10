#ifndef NS_SURFACE_HOOKS_HLSLI
#define NS_SURFACE_HOOKS_HLSLI

// 物ごとの見た目とシーンの値の使い方の口。 Common.hlsli が cbuffer ObjectCB・ViewCB と SceneValue の後に名前で include する
// これは何もしない物。 シェーダを読む時に渡したフォルダに同じ名前のファイルがあれば、 そちらが読まれる
// 書く関数と引数の形は、 差し替える物も同じにする

// 世界の位置へ足すずれ。 standard.vs と skinned.vs が world を掛けた後に足す
float3 SurfaceVertexOffset(float3 worldPos)
{
    return float3(0.0, 0.0, 0.0);
}

// 光を受ける面の向き。 lit.ps が平行光の計算に使う
float3 SurfaceNormal(float3 worldPos, float3 worldNormal)
{
    return worldNormal;
}

// 面の色に足す光。 lit.ps が平行光を掛けた色に足す
float3 SurfaceAddedLight(float3 worldPos, float3 worldNormal)
{
    return float3(0.0, 0.0, 0.0);
}

#endif
