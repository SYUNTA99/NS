// UI 矩形の PS。 UiRectCB の色をそのまま出力し、 アルファブレンドで描画済みの絵に重ねる

cbuffer UiRectCB : register(b0)
{
    float4 g_rect;
    float4 g_color;
};

float4 PSMain() : SV_Target
{
    return g_color;
}
