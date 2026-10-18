#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Windows/Window.h"

#include "NSlib/Core/Types.h"
#include <filesystem>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        //! @brief 描画システムの初期化パラメータ
        struct D3DDeviceDesc
        {
            bool enableDebugLayer = false; //!< DX11 のデバッグレイヤーを有効にするか
            bool vsync = true;             //!< 垂直同期を有効にするか
            Color clearColor{0.10f, 0.10f, 0.15f, 1.0f}; //!< BeginFrame と BeginSceneView が描画先を塗る色
        };

        class D3DCommandList;
        class D3DCommonStates;
        class D3DRenderTarget;
        class D3DTexture;
        class D3DPipeline;
        enum class BlendMode;

        //! ウィンドウに対応する D3D11 の描画基盤。構築失敗は IsValid で確認する
        //! @warning 静的変数として保持せず、GPU 資源より後に破棄する
        class D3DDevice : public NonCopyable
        {
        public:
            //! @brief 描画システムを構築する
            //! @param[in,out] window 紐づける対象のウィンドウ
            D3DDevice(const D3DDeviceDesc& desc, OS::Window& window) noexcept;
            ~D3DDevice();

            //! 描画できる状態か。構築失敗のほか、Present で device 喪失を検知した後も false になる
            [[nodiscard]] bool IsValid() const noexcept;

            //! 色と深度をクリアし、設定中の描画先でフレームを始める
            void BeginFrame(float r, float g, float b, float a) noexcept;

            //! 既定のクリア色で新しいフレームの描画を始める
            void BeginFrame() noexcept;

            //! BeginFrame と BeginSceneView が描画先を塗る色
            [[nodiscard]] const Color& ClearColor() const noexcept;

            //! フレームの描画を終了し、結果を画面に出力する
            void EndFrame() noexcept;

            //! @brief ウィンドウのサイズ変更に合わせて描画領域を再構築する
            //! @param[in] size 新しい描画領域のサイズ
            //! @note 指定された幅や高さが0以下の場合は、ウィンドウの最小化として扱われ何も行わない
            void Resize(Size2D size) noexcept;

            //! @brief 現在のシーン描画先サイズを取得する
            //! @details SetSceneTarget 設定中はその描画先、未設定時は backbuffer のサイズを返す
            [[nodiscard]] Size2D Size() const noexcept;

            //! @brief シーンの描画先をオフスクリーンへ切り替える
            //! @param[in,out] target 非所有の描画先。空の参照でウィンドウの描画先へ戻す
            //! @note 反映は次の BeginFrame から。target の破棄前に空の参照を渡して外す
            void SetSceneTarget(D3DRenderTarget* target) noexcept;

            //! 非所有の描画先とビューポートを切り替える。nullptr はバックバッファ
            //! オフスクリーンだけをクリアする。描画先の破棄後は渡さない
            void BeginSceneView(D3DRenderTarget* target) noexcept;

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
            [[nodiscard]] bool ReadBackbufferPixels(std::vector<uint8>& outBgra, Size2D& outSize) noexcept;

            //! よく使われる共通の描画ステート
            [[nodiscard]] D3DCommonStates& States() noexcept;

            //! 描画コマンドの発行やリソース更新などを記録するためのコマンドリスト
            [[nodiscard]] D3DCommandList& Commands() noexcept;

            //! @brief 各種ブレンドモードに対応する標準的な描画パイプラインを取得する
            //! @param[in] blend 取得したいブレンドモード
            //! @param[in] twoSided 裏を向いた面も描く場合 true。偽なら裏を向いた面は描かない
            //! @note 共通パイプラインは構築の最後に作るので、構築に失敗した D3DDevice では呼んではいけない
            [[nodiscard]] const D3DPipeline& CommonPipeline(BlendMode blend, bool twoSided = false) const noexcept;

            //! @brief 手前の物に隠れた画素だけへ半透明で描くパイプラインを取得する
            //! @details 深度は書かない。隠れた所を透かして見せる描画が、普通の描画と同じ物を 2 回目に描く時に使う
            //! @param[in] twoSided 裏を向いた面も描く場合 true。偽なら裏を向いた面は描かない
            //! @note 共通パイプラインと一緒に構築の最後に作るので、構築に失敗した D3DDevice では呼んではいけない
            [[nodiscard]] const D3DPipeline& OccludedPipeline(bool twoSided = false) const noexcept;

        private:
            // 描画先とビューポートを差す
            void BindTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, Size2D size) noexcept;

            ComPtr<ID3D11Device> m_device;
            ComPtr<ID3D11DeviceContext> m_context;
            ComPtr<IDXGISwapChain> m_swapchain;
            std::unique_ptr<D3DTexture> m_backbuffer;
            std::unique_ptr<D3DTexture> m_depth;
            std::unique_ptr<D3DCommandList> m_commands;
            std::unique_ptr<D3DCommonStates> m_states;
            //! 共通パイプライン。[裏も描くか][合成モード]
            std::unique_ptr<D3DPipeline> m_commonPipelines[2][3];
            //! 隠れた画素だけへ半透明で描くパイプライン。[裏も描くか]
            std::unique_ptr<D3DPipeline> m_occludedPipelines[2];
            D3DRenderTarget* m_sceneTarget = nullptr; //!< 非所有のシーン描画先。未指定ならウィンドウへ描く
            OS::Window* m_window = nullptr;
            Color m_clearColor{};
            bool m_vsync = true;
            bool m_valid = false;
            bool m_resizeCallbackRegistered = false; // デストラクタの購読解除判定
        };
    } // namespace Gfx
} // namespace NS
