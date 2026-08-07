#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Platform/Window.h"

namespace NS::Graphics
{

    //! @brief 描画システムの初期化パラメータ。
    //! @note アンチエイリアスやHDRなどの拡張設定は将来的に追加予定。
    struct RendererDesc
    {
        bool enableDebugLayer = false; //!< グラフィックスAPIのデバッグレイヤーを有効化する
        bool vsync = true;             //!< 垂直同期（V-Sync）を有効にするかどうか
        RenderSettings settings{};     //!< プロジェクト全体の標準となる描画設定
    };

    class Renderer;
    class CommandList;
    class CommonStates;
    class RenderTarget;
    class Texture;
    class Pipeline;
    class Shader;
    class Buffer;
    enum class BlendMode;

    //! @brief グラフィックスデバイスや画面出力の仕組みを管理し、描画処理全体を統括するシステム。
    //! @details アプリケーションのウィンドウと1対1で対応し、リサイズ等のイベントを自動的に処理する。
    //! 内部で保持するグラフィックスリソースの生成機能は、グローバル関数を経由して外部にも提供される。
    //! 構築に失敗した場合は例外を送出せず、無効な状態として扱う。
    //! @warning
    //! 破棄順序のバグを防ぐため、静的・グローバル変数での保持は禁止。必ずメンバ変数かスタック上で管理すること。
    class Renderer : public NS::Core::NonCopyable
    {
    public:
        //! @brief 描画システムを構築する
        //! @param desc 初期化パラメータ
        //! @param window 紐づける対象のウィンドウ
        Renderer(const RendererDesc& desc, ::NS::Platform::Window& window) noexcept;
        ~Renderer();

        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief 新しいフレームの描画準備を行う
        //! @details 画面と深度バッファをクリアし、描画先として設定する。SetSceneTarget が
        //! 設定されている間はオフスクリーン側もクリアし、そちらを描画先にする
        //! @param r クリア色の赤成分
        //! @param g クリア色の緑成分
        //! @param b クリア色の青成分
        //! @param a クリア色のアルファ成分
        void BeginFrame(float r, float g, float b, float a) noexcept;

        //! 新しいフレームの描画準備を行う
        void BeginFrame() noexcept;

        //! プロジェクト全体の標準となる描画設定を返す
        [[nodiscard]] const RenderSettings& Settings() const noexcept;

        //! フレームの描画を終了し、結果を画面に出力する
        void EndFrame() noexcept;

        //! @brief ウィンドウのサイズ変更に合わせて描画領域を再構築する
        //! @param size 新しい描画領域のサイズ
        //! @note 指定された幅や高さが0以下の場合は、ウィンドウの最小化として扱われ何も行わない
        void Resize(NS::Core::Size2D size) noexcept;

        //! @brief 現在のシーン描画先サイズを取得する
        //! @details SetSceneTarget 設定中はその描画先、未設定時は backbuffer のサイズを返す
        [[nodiscard]] NS::Core::Size2D Size() const noexcept;

        //! @brief シーンの描画先をオフスクリーンへ切り替える
        //! @param target 非所有の描画先。null で backbuffer へ戻す
        //! @note 反映は次の BeginFrame から。target を破棄する前に必ず null を渡して外すこと
        void SetSceneTarget(RenderTarget* target) noexcept;

        //! @brief 指定ターゲットを描画先にして clear + bind + viewport する
        //! @param target 非所有の描画先。null なら backbuffer へ。以降 Size() もこの描画先を返す
        //! @details 1 フレーム内で複数のオフスクリーンビューを順に描くための口
        //! BeginFrame と違い backbuffer は clear しない。破棄前の target を渡さないこと
        void BeginSceneView(RenderTarget* target) noexcept;

        //! @brief backbuffer を描画先へ戻し viewport を張り直す
        //! @details オフスクリーン描画のフレームで、UI の実描画直前に呼ぶ
        void BindBackbuffer() noexcept;

        //! よく使われる共通の描画ステート
        [[nodiscard]] CommonStates& States() noexcept;

        //! 描画コマンドの発行やリソース更新などを記録するためのコマンドリスト
        [[nodiscard]] CommandList& Commands() noexcept;

        //! @brief 各種ブレンドモードに対応する標準的な描画パイプラインを取得する
        //! @param blend 取得したいブレンドモード
        //! @note 初期化失敗時などでも、常に非nullの有効なインスタンスを返す
        [[nodiscard]] const Pipeline& CommonPipeline(BlendMode blend) const noexcept;

        /// @brief 画面全体を指定色（アルファ込み）で塗る。描画済みシーンの上へ半透明合成で重ねる
        /// @details 全画面三角形を1枚描く engine 共通の描画能力。暗転・フラッシュ等、色の意味は呼び出し側が決める
        /// 資源は初回呼び出し時に一度だけ構築し、失敗時は以後何もしない。最前面に出すため全描画の最後に呼ぶ
        void DrawFullscreenColor(const NS::Core::Color& color) noexcept;

        /// @brief 現在の描画先へ色付き矩形を 1 枚重ねる。座標は描画先のピクセルで左上原点
        /// @details 画面 UI の下地・板・ゲージを描く engine 共通の描画能力。何を表すかは呼び出し側が決める
        /// 半透明合成で最前面に出すため全描画の後に呼ぶ。資源は初回呼び出し時に一度だけ構築し、失敗時は以後何もしない
        void DrawScreenRect(float x, float y, float width, float height, const NS::Core::Color& color) noexcept;

    private:
        // 全画面塗り資源を初回だけ構築する
        void EnsureFullscreenResources() noexcept;

        // UI 矩形資源を初回だけ構築する
        void EnsureScreenRectResources() noexcept;

        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<IDXGISwapChain> m_swapchain;
        std::unique_ptr<Texture> m_backbuffer;
        std::unique_ptr<Texture> m_depth;
        std::unique_ptr<CommandList> m_commands;
        std::unique_ptr<CommonStates> m_states;
        std::unique_ptr<Pipeline> m_commonPipelines[3]; //!< 共通パイプライン
        // 全画面塗り (暗転・フラッシュ) 用の共有資源。初回 DrawFullscreenColor で一度だけ構築する
        std::unique_ptr<Shader> m_fullscreenVs;
        std::unique_ptr<Shader> m_fullscreenPs;
        std::unique_ptr<Buffer> m_fullscreenCb;
        std::unique_ptr<Pipeline> m_fullscreenPipeline;
        bool m_fullscreenTried = false; // 構築を試みたか
        bool m_fullscreenReady = false; // 構築成功
        // UI 矩形用の共有資源。初回 DrawScreenRect で一度だけ構築する
        std::unique_ptr<Shader> m_screenRectVs;
        std::unique_ptr<Shader> m_screenRectPs;
        std::unique_ptr<Buffer> m_screenRectCb;
        std::unique_ptr<Pipeline> m_screenRectPipeline;
        bool m_screenRectTried = false;        // 構築を試みたか
        bool m_screenRectReady = false;        // 構築成功
        RenderTarget* m_sceneTarget = nullptr; //!< 非所有のシーン描画先。null なら backbuffer へ描く
        ::NS::Platform::Window* m_window = nullptr;
        RenderSettings m_settings{};
        bool m_vsync = true;
        bool m_valid = false;
        bool m_resizeCallbackRegistered = false; // デストラクタの購読解除判定
    };

} // namespace NS::Graphics
