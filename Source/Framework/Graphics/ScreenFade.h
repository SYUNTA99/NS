#pragma once

/// @file ScreenFade.h
/// @brief NS::Graphics::ScreenFade — 画面全体を単色 + alpha で覆うフルスクリーン overlay
///
/// @details 頂点バッファを使わず SV_VertexID から覆い尽くす三角形を生成する fade.vs/ps.hlsl と、
/// 色 + alpha を渡す 16 byte の定数バッファを保持する。 alpha ブレンドで描画済みシーンの上へ重ね、
/// レベル遷移やクリア / 死亡時の暗転 / 明転に使う。 深度比較は無効で常に最前面、 全画面三角形なので
/// カリングも無効にする。 構築失敗時は IsValid()==false となり Render は何もしない
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

    /// 全画面を単色 + alpha で覆う overlay。 暗転 / 明転に使う。 Renderer より先に破棄すること
    class ScreenFade : public NS::Core::NonCopyable
    {
    public:
        /// ScreenFade を生成する。 shader / 定数バッファ / pipeline を構築して返す
        [[nodiscard]] static std::unique_ptr<ScreenFade> Create();

        ~ScreenFade();

        /// color.rgb の単色を color.a の不透明度で全画面に重ねる。 不透明描画後・最前面に呼ぶこと
        /// color.a==0 でも描画は走るため、 不要なフレームは呼び出し側で省くこと
        void Render(Renderer& renderer, const NS::Math::Color& color) noexcept;

        /// shader / 定数バッファ / pipeline が揃って構築できていれば true。 Device 不在や構築失敗で false
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
