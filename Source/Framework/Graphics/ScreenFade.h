#pragma once

/// @file ScreenFade.h
/// @brief NS::Graphics::ScreenFade — 画面全体を単色と不透明度で覆う重ね描画
///
/// @details 頂点バッファを使わず SV_VertexID から全画面の三角形を生成する fade.vs/ps.hlsl と、
/// 色を渡す 16 バイトの定数バッファを保持する。 アルファブレンドで描画済みシーンへ重ね、 暗転と明転を作る
/// 深度比較も全画面三角形のカリングも無効で常に最前面に出る。 構築失敗時は IsValid() が false で何もしない
/// GPU バインドは Render に渡す Renderer 経由で行い、 DeviceContext は保持しない

#include <memory>

#include "Framework/Core/NonCopyable.h"
#include "Framework/Math/Math.h"

namespace NS::Graphics
{

    class Renderer;
    class Shader;
    class Buffer;
    class Pipeline;

    /// 全画面を単色と不透明度で覆う。 暗転と明転に使い、 Renderer より先に破棄する
    class ScreenFade : public NS::Core::NonCopyable
    {
    public:
        /// ScreenFade を生成する。 シェーダと定数バッファとパイプラインを構築して返す
        [[nodiscard]] static std::unique_ptr<ScreenFade> Create();

        ~ScreenFade();

        /// color.rgb の色を color.a の不透明度で全画面に重ねる。 不透明描画の後に最前面で呼ぶ
        /// color.a が 0 でも描画は走るので、 不要なフレームは呼び出し側で省く
        void Render(Renderer& renderer, const NS::Math::Color& color) noexcept;

        /// シェーダと定数バッファとパイプラインが揃えば true。 Device 不在や構築失敗で false
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        ScreenFade();

        std::unique_ptr<Shader> m_vs;
        std::unique_ptr<Shader> m_ps;
        std::unique_ptr<Buffer> m_cb;
        std::unique_ptr<Pipeline> m_pipeline;
        bool m_valid = false;
    };

} // namespace NS::Graphics
