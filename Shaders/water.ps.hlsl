#include "Common.hlsli"
#include "water.hlsli"

// SceneRenderer::DrawAfterSky が結ぶ。結べない時は GetDimensions が 0 を返す
Texture2D<float> g_sceneDepth : register(t8);
TextureCube g_skyCube : register(t9);
SamplerState g_skySampler : register(s1);

static const float k_NoShoreDistance = 100000.0;

float2 PerlinGradient(float2 cell)
{
    float3 q = frac(float3(cell.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    float angle = frac((q.x + q.y) * q.z) * k_WaterTwoPi;
    return float2(cos(angle), sin(angle));
}

float PerlinNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = p - cell;
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(PerlinGradient(cell), f);
    float b = dot(PerlinGradient(cell + float2(1, 0)), f - float2(1, 0));
    float c = dot(PerlinGradient(cell + float2(0, 1)), f - float2(0, 1));
    float d = dot(PerlinGradient(cell + float2(1, 1)), f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm(float2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        sum += PerlinNoise(p) * amplitude;
        p = float2(p.x * 1.6 - p.y * 1.2, p.x * 1.2 + p.y * 1.6);
        amplitude *= 0.5;
    }
    return sum;
}

// 細かい揺れを持たない 2 段のノイズ。泡の線を滑らかな曲線にする
float SmoothNoise(float2 p)
{
    return PerlinNoise(p) * 0.75 + PerlinNoise(float2(p.x * 1.6 - p.y * 1.2, p.x * 1.2 + p.y * 1.6) + 3.7) * 0.25;
}

// 視線に沿って水面の奥の物までの距離を測る。遠い物と空は k_NoShoreDistance
float ShoreDistance(float4 screenPos, float3 worldPos, float3 viewDirection, float distanceToWater)
{
    uint width;
    uint height;
    g_sceneDepth.GetDimensions(width, height);
    if (width == 0)
    {
        return k_NoShoreDistance;
    }
    float depth = g_sceneDepth.Load(int3(screenPos.xy, 0));
    if (depth >= 1.0)
    {
        return k_NoShoreDistance;
    }
    float4 cameraClip = mul(float4(g_cameraPosition, 1.0), viewProj);
    float4 rayClip = mul(float4(viewDirection, 0.0), viewProj);
    float denominator = depth * rayClip.w - rayClip.z;
    if (abs(denominator) < 0.000001)
    {
        return k_NoShoreDistance;
    }
    float distanceToScene = (cameraClip.z - depth * cameraClip.w) / denominator;
    // 浅い角度の水面で泡が広がりすぎないよう、視線の傾きで縮める
    return max(distanceToScene - distanceToWater, 0.0) * max(abs(viewDirection.y), 0.25);
}

float3 SkyColor(float3 direction, float3 fallback)
{
    uint width;
    uint height;
    uint mips;
    g_skyCube.GetDimensions(0, width, height, mips);
    if (width == 0)
    {
        return fallback;
    }
    return g_skyCube.SampleLevel(g_skySampler, direction, skyBlurMip).rgb;
}

// 波の進む向きに並ぶ波頭の列。山は細く、谷は広くして、波の列として読めるようにする
float WaveRidge(float2 p, float time, float angle, float wavelength, inout float2 slope)
{
    float2 direction = SwellDirection(angle);
    float k = k_WaterTwoPi / wavelength;
    float phase = dot(p, direction) * k - time * sqrt(k_WaterGravity * k) * swellSpeed;
    slope += direction * cos(phase) * 0.12;
    float wave = 0.5 + 0.5 * sin(phase);
    return wave * wave * wave;
}

float4 PSMain(WaterInterp input) : SV_TARGET
{
    float2 p = input.basePos;
    float time = g_sceneTimeSeconds;
    float2 offset;
    float swell;
    float2 slope;
    WaterSwell(p, time, offset, swell, slope);
    float crest = swell / max(swellHeight, 0.001);

    float3 toCamera = g_cameraPosition - input.worldPos;
    float distanceToWater = length(toCamera);
    float3 view = toCamera / max(distanceToWater, 0.001);

    // 波の進む向きへ流しながら、座標を fBm で歪ませて色面の形を崩す
    float2 flow = SwellDirection(0.0) * time * swellSpeed * 1.5;
    float2 warpInput = (p - flow) * bandScale;
    float2 warp = float2(Fbm(warpInput), Fbm(warpInput + float2(5.2, 1.3)));
    float2 warped = p - flow + warp * warpStrength;
    // 色面は波の進む向きに短く、波頭の向きに長く伸ばして、丸い塊に見せない
    float2 waveAxis = SwellDirection(0.0);
    float2 stretched = float2(dot(warped, waveAxis), dot(warped, float2(-waveAxis.y, waveAxis.x)) * 0.3);

    // 波頭の列。歪ませた座標で測り、直線の縞に見せない
    float2 ridgeSlope = float2(0, 0);
    float2 ridgePos = p + warp * warpStrength * 0.6;
    float ridges = WaveRidge(ridgePos, time, 0.0, waveLength, ridgeSlope) * 0.5
                 + WaveRidge(ridgePos, time, 0.5, waveLength * 0.62, ridgeSlope) * 0.3
                 + WaveRidge(ridgePos, time, -0.4, waveLength * 0.41, ridgeSlope) * 0.2;
    float ridgeFade = 1.0 - smoothstep(foamFadeDistance * 0.6, foamFadeDistance * 1.4, distanceToWater);
    slope += ridgeSlope * ridgeFade;
    crest += (ridges - 0.25) * 2.0 * ridgeFade;

    float bandField = Fbm(stretched * bandScale) + crest * 0.06;
    float3 water = lerp(deepColor, midColor, smoothstep(-0.08 - bandSoftness, -0.08 + bandSoftness, bandField));
    water = lerp(water, brightColor, smoothstep(0.10 - bandSoftness, 0.10 + bandSoftness, bandField));
    // 山を明るく、谷を暗くする
    float ridgeBand = smoothstep(0.34 - bandSoftness, 0.34 + bandSoftness, ridges);
    float troughBand = 1.0 - smoothstep(0.05, 0.05 + bandSoftness * 2.0, ridges);
    water = lerp(water, brightColor, ridgeBand * waveContrast * ridgeFade);
    water = lerp(water, deepColor, troughBand * waveContrast * 0.5 * ridgeFade);
    // 上から見ると高低が読めないので、太陽を向く面と背を向ける面で色の段を分ける
    float2 sunAxis = -lightDir.xz / max(length(lightDir.xz), 0.001);
    float facing = dot(ridgeSlope, sunAxis);
    float lit = smoothstep(0.02 - bandSoftness, 0.02 + bandSoftness, facing);
    float shaded = 1.0 - smoothstep(-0.05 - bandSoftness, -0.05 + bandSoftness, facing);
    water = lerp(water, brightColor, lit * waveContrast * 0.45 * ridgeFade);
    water = lerp(water, deepColor, shaded * waveContrast * 0.55 * ridgeFade);

    float shore = ShoreDistance(input.pos, input.worldPos, -view, distanceToWater);
    water = lerp(shallowColor, water, smoothstep(0.0, max(shoreColorWidth, 0.1), shore));

    // 面の向きはうねりの傾きに、細かい fBm の揺らぎを近くでだけ足す
    float detailFade = 1.0 - smoothstep(60.0, 200.0, distanceToWater);
    float2 detailSlope = float2(Fbm(warped * 0.25), Fbm(warped * 0.25 + 7.7)) * 0.08 * detailFade;
    float3 normal = normalize(float3(-slope.x - detailSlope.x, 1.0, -slope.y - detailSlope.y));

    float fresnel = 0.03 + 0.97 * pow(1.0 - saturate(dot(view, normal)), 5.0);
    float3 reflected = reflect(-view, normal);
    reflected.y = abs(reflected.y);
    float3 color = lerp(water, SkyColor(reflected, water), saturate(fresnel * reflectionStrength * 2.0));

    // 沖合の泡はノイズの等高線。傾きで割って等高線までの距離 (m) にし、線の太さを揃える
    float2 foamPos = p - flow;
    foamPos += float2(SmoothNoise(foamPos * 0.02), SmoothNoise(foamPos * 0.02 + float2(5.2, 1.3))) * warpStrength;
    float foamField = SmoothNoise(foamPos * foamScale);
    float2 foamGradient = float2(SmoothNoise((foamPos + float2(0.5, 0.0)) * foamScale) - foamField,
                                 SmoothNoise((foamPos + float2(0.0, 0.5)) * foamScale) - foamField) / 0.5;
    float foamDistance = abs(foamField) / max(length(foamGradient), 0.001);
    float foamAa = max(fwidth(foamDistance), 0.001);
    // 波頭に近い所でだけ太くなり、波が下がると細くなって消える
    float crestMask = smoothstep(foamCrestStart, foamCrestStart + 0.45, crest + SmoothNoise(foamPos * 0.012) * 1.2);
    float foamLine = 1.0 - smoothstep(foamLineWidth * crestMask, foamLineWidth * crestMask + foamAa, foamDistance);
    foamLine *= smoothstep(0.02, 0.2, crestMask);
    // 傾きが小さい所は距離が小さく出て点が残るので、値が 0 に近い所だけを線にする
    foamLine *= 1.0 - smoothstep(0.03, 0.06, abs(foamField));
    foamLine *= 1.0 - smoothstep(foamFadeDistance * 0.4, foamFadeDistance, distanceToWater);
    color = lerp(color, foamColor, saturate(foamLine));

    // 岸辺は深度の距離の帯が寄せては引く
    if (shore < k_NoShoreDistance * 0.5)
    {
        float shoreNoise = Fbm(p * 0.35 + float2(time * 0.08, -time * 0.05));
        float wobble = shore + shoreNoise * shoreFoamWidth * 0.8;
        float edge = 1.0 - smoothstep(shoreFoamWidth * 0.25, shoreFoamWidth * 0.5, wobble);
        float wave = sin(wobble / max(shoreFoamWidth, 0.1) * shoreWaveCount * k_WaterTwoPi + time * shoreWaveSpeed * k_WaterTwoPi);
        float bands = smoothstep(0.55, 0.8, wave) * (1.0 - smoothstep(shoreFoamWidth * 0.5, shoreFoamWidth * 1.6, wobble));
        color = lerp(color, foamColor, saturate(max(edge, bands)));
    }

    // 太陽の向きにだけ小さい光を足す
    float3 toLight = -normalize(lightDir);
    float sun = pow(saturate(dot(normal, normalize(view + toLight))), max(sunSize, 1.0));
    color += foamColor * smoothstep(0.3, 0.7, sun) * sunStrength;

    // 遠くの水面は、見ている向きの水平線の空の色へ寄せる
    float3 horizonDirection = normalize(float3(-view.x, 0.02, -view.z));
    float horizonBlend = smoothstep(horizonStart, max(horizonEnd, horizonStart + 1.0), distanceToWater);
    color = lerp(color, SkyColor(horizonDirection, color), horizonBlend);
    return float4(color, 1.0);
}
