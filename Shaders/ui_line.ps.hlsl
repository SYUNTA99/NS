// UI の線の PS。 UiLineCB の色をそのまま出力し、 アルファブレンドで描画済みの絵に重ねる

cbuffer UiLineCB : register(b0)
{
    float4 g_cornersA;
    float4 g_cornersB;
    float4 g_color;
};

float4 PSMain() : SV_Target
{
    return g_color;
}
