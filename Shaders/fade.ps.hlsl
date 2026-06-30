// 全画面フェードの PS。 FadeCB の色をそのまま出力する。 アルファブレンドで描画済みシーンに重ねて
// 暗転と明転を作る。 黒かつ不透明度 1 で全黒、 不透明度 0 で背景のまま残る

cbuffer FadeCB : register(b0)
{
    float4 g_color;
};

float4 PSMain() : SV_Target
{
    return g_color;
}
