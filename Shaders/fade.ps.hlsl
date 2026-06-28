// 全画面フェード PS。 CB の単色 + alpha をそのまま出力する。 alpha ブレンドで描画済みシーンに重ね、
// 暗転 / 明転を作る。 黒なら rgb=0 のため result = 背景 * (1-alpha) となり alpha=1 で全黒になる。

cbuffer FadeCB : register(b0)
{
    float4 g_color;
};

float4 PSMain() : SV_Target
{
    return g_color;
}
