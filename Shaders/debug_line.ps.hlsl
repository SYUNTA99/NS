// stub: 入力 color をそのまま出力。
struct PSIn
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4 PSMain(PSIn IN) : SV_TARGET
{
    return IN.color;
}
