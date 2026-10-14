#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"
#include "NSlib/Graphics/RenderSettings.h"
#include "NSlib/Windows/Window.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace NS
{
    class CameraData;
} // namespace NS

namespace NS::Gfx
{

    //! @brief 描画システムの初期化パラメータ
    struct RendererDesc
    {
        bool enableDebugLayer = false; //!< DX11 のデバッグレイヤーを有効にするか
        bool vsync = true;             //!< 垂直同期を有効にするか
        RenderSettings settings{};     //!< プロジェクト全体の標準となる描画設定
    };

    class CommandList;
    class CommonStates;
    class RenderTarget;
    class Texture;
    class Pipeline;
    class Shader;
    class Buffer;
    class Skybox;
    enum class BlendMode;

    //! ウィンドウに対応する D3D11 の描画基盤。構築失敗は IsValid で確認する
    //! @warning 静的変数として保持せず、GPU 資源より後に破棄する
    class Renderer : public NS::NonCopyable
    {
    public:
        //! @brief 描画システムを構築する
        //! @param[in,out] window 紐づける対象のウィンドウ
        Renderer(const RendererDesc& desc, ::NS::OS::Window& window) noexcept;
        ~Renderer();

        //! 描画できる状態か。構築失敗のほか、Present で device 喪失を検知した後も false になる
        [[nodiscard]] bool IsValid() const noexcept;

        //! 色と深度をクリアし、設定中の描画先でフレームを始める
        void BeginFrame(float r, float g, float b, float a) noexcept;

        //! 既定のクリア色で新しいフレームの描画を始める
        void BeginFrame() noexcept;

        //! プロジェクト全体の標準となる描画設定を返す
        [[nodiscard]] const RenderSettings& Settings() const noexcept;

        //! フレームの描画を終了し、結果を画面に出力する
        void EndFrame() noexcept;

        //! @brief ウィンドウのサイズ変更に合わせて描画領域を再構築する
        //! @param[in] size 新しい描画領域のサイズ
        //! @note 指定された幅や高さが0以下の場合は、ウィンドウの最小化として扱われ何も行わない
        void Resize(NS::Size2D size) noexcept;

        //! @brief 現在のシーン描画先サイズを取得する
        //! @details SetSceneTarget 設定中はその描画先、未設定時は backbuffer のサイズを返す
        [[nodiscard]] NS::Size2D Size() const noexcept;

        //! @brief シーンの描画先をオフスクリーンへ切り替える
        //! @param[in,out] target 非所有の描画先。空の参照でウィンドウの描画先へ戻す
        //! @note 反映は次の BeginFrame から。target の破棄前に空の参照を渡して外す
        void SetSceneTarget(RenderTarget* target) noexcept;

        //! 非所有の描画先とビューポートを切り替える。nullptr はバックバッファ
        //! オフスクリーンだけをクリアする。描画先の破棄後は渡さない
        void BeginSceneView(RenderTarget* target) noexcept;

        //! @brief backbuffer を描画先へ戻し viewport を張り直す
        //! @details オフスクリーン描画のフレームで、UI の実描画直前に呼ぶ
        void BindBackbuffer() noexcept;

        //! @brief 今の backbuffer の内容を PNG として書き出す
        //! @details 書き出しは WIC を通る。IsValid() が false の間は何もしない
        //! @param[in] path 書き出し先のファイルパス。親フォルダは作らない
        //! @return 書き出せた場合 true、それ以外の場合は false
        [[nodiscard]] bool CaptureBackbufferToPng(const std::filesystem::path& path) noexcept;

        //! バックバッファを左上から行順の BGRA と画素サイズへ読み出す
        //! 失敗時は出力を変更せず false。無効な描画基盤や変換できない形式では読まない
        [[nodiscard]] bool ReadBackbufferPixels(std::vector<std::uint8_t>& outBgra, NS::Size2D& outSize) noexcept;

        //! よく使われる共通の描画ステート
        [[nodiscard]] CommonStates& States() noexcept;

        //! 描画コマンドの発行やリソース更新などを記録するためのコマンドリスト
        [[nodiscard]] CommandList& Commands() noexcept;

        //! @brief 各種ブレンドモードに対応する標準的な描画パイプラインを取得する
        //! @param[in] blend 取得したいブレンドモード
        //! @param[in] twoSided 裏を向いた面も描く場合 true。偽なら裏を向いた面は描かない
        //! @note 共通パイプラインは構築の最後に作るので、構築に失敗した Renderer では呼んではいけない
        [[nodiscard]] const Pipeline& CommonPipeline(BlendMode blend, bool twoSided = false) const noexcept;

        //! @brief 手前の物に隠れた画素だけへ半透明で描くパイプラインを取得する
        //! @details 深度は書かない。隠れた所を透かして見せる描画が、普通の描画と同じ物を 2 回目に描く時に使う
        //! @param[in] twoSided 裏を向いた面も描く場合 true。偽なら裏を向いた面は描かない
        //! @note 共通パイプラインと一緒に構築の最後に作るので、構築に失敗した Renderer では呼んではいけない
        [[nodiscard]] const Pipeline& OccludedPipeline(bool twoSided = false) const noexcept;

        //! 全画面に指定色を半透明合成する。最前面に出す場合は最後に呼ぶ
        void DrawFullscreenColor(const NS::Color& color) noexcept;

        //! 左上原点の画素座標で矩形を半透明合成する。最前面に出す場合は最後に呼ぶ
        void DrawScreenRect(float x, float y, float width, float height, const NS::Color& color) noexcept;

        //! 不透明の後、半透明の前に空を描く。ContentRoot 内の .dds またはキューブマップのディレクトリを使う
        //! 空パスでは描かない。画像読込失敗は前の画像を保って再試行し、GPU 資源の生成失敗は再試行しない
        void DrawSky(const NS::CameraData& camera, std::string_view cubemapPath) noexcept;

    private:
        // 頂点を持たず SV_VertexID から形を作り、最前面へ半透明で重ねる描画の資源
        struct OverlayResources
        {
            std::unique_ptr<Shader> vs;
            std::unique_ptr<Shader> ps;
            std::unique_ptr<Buffer> cb;
            std::unique_ptr<Pipeline> pipeline;
            bool tried = false; // 構築を試みたか
            bool ready = false; // 構築成功
        };

        // 重ね描きの資源を初回だけ構築し、今描けるかを返す。label はログに出す名前
        [[nodiscard]] bool EnsureOverlay(OverlayResources& overlay,
                                         std::string_view vsFileName,
                                         std::string_view psFileName,
                                         std::size_t constantBytes,
                                         std::string_view label) noexcept;

        // 定数を流して重ね描きを 1 回出す。vsReadsConstants が false なら頂点シェーダへ定数を差さない
        void DrawOverlay(const OverlayResources& overlay,
                         const void* constants,
                         std::size_t constantBytes,
                         unsigned vertexCount,
                         bool vsReadsConstants) noexcept;

        // 描画先とビューポートを差す
        void BindTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, NS::Size2D size) noexcept;

        // 空を描く Skybox を初回だけ構築する
        void EnsureSkyboxResources() noexcept;

        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<IDXGISwapChain> m_swapchain;
        std::unique_ptr<Texture> m_backbuffer;
        std::unique_ptr<Texture> m_depth;
        std::unique_ptr<CommandList> m_commands;
        std::unique_ptr<CommonStates> m_states;
        //! 共通パイプライン。[裏も描くか][合成モード]
        std::unique_ptr<Pipeline> m_commonPipelines[2][3];
        //! 隠れた画素だけへ半透明で描くパイプライン。[裏も描くか]
        std::unique_ptr<Pipeline> m_occludedPipelines[2];
        OverlayResources m_fullscreen; // 全画面塗り。初回 DrawFullscreenColor で構築
        OverlayResources m_screenRect; // UI 矩形。初回 DrawScreenRect で構築
        // 空を描く Skybox。cubemap を指定した初回の DrawSky で一度だけ構築する
        std::unique_ptr<Skybox> m_skybox;
        std::string m_loadedSkyboxPath; // 前回判定した cubemap のパス。読めた物と拒否した物が入り、差分の時だけ読み直す
        bool m_skyboxTried = false;     // 構築を試みたか
        RenderTarget* m_sceneTarget = nullptr; //!< 非所有のシーン描画先。未指定ならウィンドウへ描く
        ::NS::OS::Window* m_window = nullptr;
        RenderSettings m_settings{};
        bool m_vsync = true;
        bool m_valid = false;
        bool m_resizeCallbackRegistered = false; // デストラクタの購読解除判定
    };

} // namespace NS::Gfx
