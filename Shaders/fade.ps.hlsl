// 全画面塗りの PS。 FadeCB の色をそのまま出力する。 アルファブレンドで描画済みシーンに重ねる
// 不透明度 1 でその色一色、 不透明度 0 で背景のまま残る

cbuffer FadeCB : register(b0)
{
    float4 g_color;
};

float4 PSMain() : SV_Target
{
    return g_color;
}
