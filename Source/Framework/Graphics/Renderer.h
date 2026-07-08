#pragma once

/// @file Renderer.h
/// @brief NS::Graphics::Renderer — D3D11 Device / DeviceContext / SwapChain を
/// 所有する描画ファサード
///
/// @details Window と 1 対 1 で生成し、 Window のリサイズ通知を購読する
/// device / context / swapchain は公開ヘッダ `D3dCommon.h` 経由で D3D 型を取り込み ComPtr メンバで直接保持する
/// リソース生成で要る Device / Context はプロセスグローバルの `Gpu()` で引く
/// 構築失敗時は `IsValid() == false` を返し例外は投げない。 詳細は `NS_LOG_ERROR` へ出力する
/// @warning プロセス唯一の device 前提。 Renderer を static / グローバル変数として持つと `Gpu()` の
/// グローバルとの破棄順序が未定義になるため、 必ずスタックまたは他オブジェクトのメンバとして所有すること

#include <cstddef>
#include <memory>

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Platform/Window.h"

namespace NS::Graphics
{

    /// Renderer 構築パラメータ。現状は最小フィールドのみ
    /// MSAA / HDR / FEATURE_LEVEL 切替は将来追加予定
    struct RendererDesc
    {
        /// D3D11 Debug Layer を有効化する。Debug ビルドで true 推奨
        bool enableDebugLayer = false;
        /// Present 時の V-Sync。true で SyncInterval=1、false で 0
        bool vsync = true;
        /// クリア色やライティングを含むプロジェクト描画既定値。シーンはこれを基に override を Resolve する
        RenderSettings settings{};
    };

    class Renderer;
    class CommandList;
    class CommonStates;
    class Texture;
    class Pipeline;
    enum class BlendMode;

    /// D3D11 Device / DeviceContext / SwapChain を所有するレンダラ。Window と 1 対 1 で生成しリサイズ通知を購読
    /// リソース生成側は Device / Context をプロセスグローバルの Gpu() で引く
    class Renderer : public NS::Core::NonCopyable
    {
    public:
        Renderer(const RendererDesc& desc, ::NS::Platform::Window& window) noexcept;
        ~Renderer();

        /// 構築成功判定。D3D11CreateDevice / SwapChain 作成失敗時に false
        [[nodiscard]] bool IsValid() const noexcept;

        /// フレーム頭で呼ぶ。backbuffer と depth をクリアし、 描画先としてバインドする
        void BeginFrame(float r, float g, float b, float a) noexcept;

        /// クリア色に Settings().clearColor を使う省略形
        void BeginFrame() noexcept;

        /// RendererDesc.settings が持つプロジェクト描画既定値。シーンはこれを基に override を Resolve する
        [[nodiscard]] const RenderSettings& Settings() const noexcept;

        /// フレーム末で呼ぶ。SwapChain::Present を実行する
        void EndFrame() noexcept;

        /// SwapChain::ResizeBuffers + backbuffer / depth Texture の再構築。Window リサイズで自動呼出される
        /// size.width または size.height が 0 以下なら何もせず最小化に対応する
        void Resize(NS::Math::Size2D size) noexcept;

        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        /// DirectXTK CommonStates ラッパ。Mesh/Material 等が利用
        [[nodiscard]] CommonStates& States() noexcept;

        /// bind / draw / update を記録する CommandList。 context 呼び出しは全てここに集約される
        [[nodiscard]] CommandList& Commands() noexcept;

        /// BlendMode の Opaque / Alpha / Additive に対応する共通 Pipeline。初期化時に1回だけ生成してキャッシュする
        /// 描画する者が DrawCall 直前に SetPipeline で適用する。返り値は常に有効なインスタンスで失敗時も非 null
        [[nodiscard]] const Pipeline& CommonPipeline(BlendMode blend) const noexcept;

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<IDXGISwapChain> m_swapchain;
        std::unique_ptr<Texture> m_backbuffer;
        std::unique_ptr<Texture> m_depth;
        std::unique_ptr<CommandList> m_commands;
        std::unique_ptr<CommonStates> m_states;
        // 共通 Pipeline。0 が Opaque、1 が Alpha、2 が Additive。コンストラクタで生成し CommonPipeline で引く
        std::unique_ptr<Pipeline> m_commonPipelines[3];
        ::NS::Platform::Window* m_window = nullptr;
        RenderSettings m_settings{};
        bool m_vsync = true;
        bool m_valid = false;
        // デストラクタの購読解除判定は「登録したか」で行う。Resize 失敗で m_valid が落ちても解除は必要
        bool m_resizeCallbackRegistered = false;
    };

} // namespace NS::Graphics
