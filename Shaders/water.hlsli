#ifndef NS_WATER_HLSLI
#define NS_WATER_HLSLI

cbuffer MaterialParams : register(b2)
{
    float swellHeight = 2.4;
    float swellSpeed = 0.65;
    float2 swellDirection = float2(0.8, 0.6);
    float3 deepColor = float3(0.035, 0.34, 0.62);
    float bandScale = 0.03;
    float3 midColor = float3(0.05, 0.45, 0.70);
    float bandSoftness = 0.02;
    float3 brightColor = float3(0.08, 0.58, 0.78);
    float warpStrength = 12.0;
    float3 shallowColor = float3(0.20, 0.78, 0.82);
    float foamLineWidth = 0.35;
    float3 foamColor = float3(0.90, 0.97, 0.98);
    float foamScale = 0.06;
    float foamCrestStart = 0.0;
    float foamFadeDistance = 340.0;
    float shoreFoamWidth = 3.5;
    float shoreWaveCount = 3.0;
    float shoreWaveSpeed = 0.8;
    float shoreColorWidth = 7.0;
    float reflectionStrength = 0.35;
    float skyBlurMip = 3.0;
    float horizonStart = 110.0;
    float horizonEnd = 450.0;
    float sunSize = 4000.0;
    float sunStrength = 0.2;
    float waveLength = 38.0;
    float waveContrast = 1.0;
    // うねりの 3 本の波。x は swellDirection から回す角度 (ラジアン)、y は波長 (m)、z は swellHeight に掛ける割合。
    // 水面の高さはゲームのコード (Game/Level/WaterSwell) も同じ式で計算するので、数は .mat に置いて両方が読む
    float3 swellWaveA = float3(0.0, 113.0, 0.55);
    float swellGravity = 9.8;
    float3 swellWaveB = float3(0.45, 73.0, 0.30);
    float3 swellWaveC = float3(-0.6, 52.0, 0.15);
};

struct WaterInterp
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 basePos : TEXCOORD1;
};

static const float k_WaterTwoPi = 6.2831853;

// swellDirection を angle だけ回した単位の向き。swellDirection が 0 の時は 0 を返し、非数にしない
float2 SwellDirection(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    float2 direction = float2(swellDirection.x * c - swellDirection.y * s,
                              swellDirection.x * s + swellDirection.y * c);
    return direction / max(length(direction), 0.0001);
}

// 波長ごとの速さは水の波の式 (深い水) で決め、長い波ほど速く進む
void AddSwell(float2 p, float time, float angle, float wavelength, float amplitude,
              inout float2 offset, inout float height, inout float2 slope)
{
    float2 direction = SwellDirection(angle);
    float k = k_WaterTwoPi / wavelength;
    float omega = sqrt(swellGravity * k) * swellSpeed;
    float phase = dot(p, direction) * k - time * omega;
    // ゲルストナー波。頂点を波頭へ寄せて、谷を広く山を尖らせる
    float steepness = 0.6 / max(k * amplitude * 3.0, 1.0);
    offset += direction * cos(phase) * amplitude * steepness;
    height += sin(phase) * amplitude;
    slope += direction * cos(phase) * amplitude * k;
}

void WaterSwell(float2 p, float time, out float2 offset, out float height, out float2 slope)
{
    offset = float2(0, 0);
    height = 0.0;
    slope = float2(0, 0);
    AddSwell(p, time, swellWaveA.x, swellWaveA.y, swellHeight * swellWaveA.z, offset, height, slope);
    AddSwell(p, time, swellWaveB.x, swellWaveB.y, swellHeight * swellWaveB.z, offset, height, slope);
    AddSwell(p, time, swellWaveC.x, swellWaveC.y, swellHeight * swellWaveC.z, offset, height, slope);
}

#endif
