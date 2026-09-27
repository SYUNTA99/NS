// 溜めている間の地面の矢印の PS。帯と矢じりの板に、役ごとの覆いの層の絵を貼って色を掛ける
// 絵の r は明るい縁、g は塗り (塗りの画素の平均が 0.25 になるように焼いてある)、b は暗い縁の覆い
// 帯の絵の a は、その横の位置で帯が矢じりの下へ潜って切れる所 (矢じりの先 0 から後ろの端 1 への割合)。矢じりの絵の a は 0
// 光と露出は掛けない。見本の色をそのまま画面の値として出す
// 頂点シェーダは standard.vs.hlsl。cbuffer は FrameCB と同じ大きさで world と viewProj の位置を揃え、
// 残りは Source/Game/Level/SlamArrow.cpp の GroundArrowConstants と同じ並び

cbuffer GroundArrowCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float4 chargedColor; // rgb は色の付いた部分の色、a は明るい縁の不透明度
    float4 plainColor;   // rgb は色の付いていない部分の色、a は明るい縁の不透明度
    float4 darkColor;    // rgb は暗い縁の色、a はその不透明度
    float4 fadeAndFront; // xy は始まりのぼかし、zw は色の付いた部分の重み。どちらも板の v の 1 次式
    float4 rearAndFill;  // xy は矢じりの先からの距離 ÷ 奥行き (板の v の 1 次式)、z と w は塗りの平均の不透明度
};

// standard.vs.hlsl の出力と同じ並び
struct SurfaceInterp
{
    float4 pos         : SV_POSITION;
    float2 uv          : TEXCOORD;
    float3 worldNormal : NORMAL;
};

Texture2D    layers : register(t0);
SamplerState samp   : register(s0);

// 塗りの層は平均 0.25 に焼いてある。4 倍すると塗りの平均が 1 になる
static const float k_FillScale = 4.0;
// 長い向きの足跡を最大でこの数に割って引く。板を前へ倒して見た時に、縦の縮みで粗い縮小の段へ落ちるのを止める
static const float k_MaxTaps = 4.0;

// 画面の 1 画素が絵の上で覆う足跡の長い向きを k_MaxTaps 個まで割り、割った足跡ごとに引いて平均する
// サンプラは等方の線形なので、そのままでは長い向きの長さで縮小の段が決まり、帯と矢じりの縁がぼける
float4 SampleLayers(float2 uv)
{
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    float width;
    float height;
    layers.GetDimensions(width, height);
    float2 size = float2(width, height);
    float lengthX = length(dx * size);
    float lengthY = length(dy * size);
    float2 major = dx;
    float2 minor = dy;
    float majorLength = lengthX;
    float minorLength = lengthY;
    if (lengthY > lengthX)
    {
        major = dy;
        minor = dx;
        majorLength = lengthY;
        minorLength = lengthX;
    }
    float taps = clamp(ceil(majorLength / max(minorLength, 1e-4)), 1.0, k_MaxTaps);
    float2 subFootprint = major / taps;
    // 割った足跡の真ん中を並べる。taps が 1 なら全部が真ん中に重なる
    float spread = (1.0 - 1.0 / taps) * (k_MaxTaps / (k_MaxTaps - 1.0));
    float4 sum = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float t = ((i + 0.5) / k_MaxTaps - 0.5) * spread;
        sum += layers.SampleGrad(samp, uv + major * t, subFootprint, minor);
    }
    return sum / k_MaxTaps;
}

float4 PSMain(SurfaceInterp input) : SV_TARGET
{
    float4 m = SampleLayers(input.uv);
    float v = input.uv.y;

    float fade = saturate(fadeAndFront.x + fadeAndFront.y * v);
    float charged = saturate(fadeAndFront.z + fadeAndFront.w * v);
    // 帯の切れ目。1 画素の幅でなめらかに切る
    float rear = rearAndFill.x + rearAndFill.y * v;
    float edgeWidth = max(fwidth(rear), 1e-4);
    float keep = saturate((rear - m.a) / edgeWidth + 0.5);

    float fill = m.g * k_FillScale;
    float chargedAlpha = m.r * chargedColor.a + fill * rearAndFill.z;
    float plainAlpha = m.r * plainColor.a + fill * rearAndFill.w;
    float darkAlpha = m.b * darkColor.a;

    // 不透明度を掛けた色で混ぜ、最後に不透明度で割って半透明の合成へ渡す
    float3 premultiplied = chargedColor.rgb * (chargedAlpha * charged) + plainColor.rgb * (plainAlpha * (1.0 - charged)) +
                           darkColor.rgb * darkAlpha;
    float alpha = chargedAlpha * charged + plainAlpha * (1.0 - charged) + darkAlpha;
    float cover = fade * keep;
    alpha *= cover;
    premultiplied *= cover;
    if (alpha <= 1e-4)
    {
        discard;
    }
    float outAlpha = saturate(alpha);
    return float4(premultiplied / alpha, outAlpha);
}
