#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <string>

namespace NS
{
    namespace Gfx
    {
        //! path があれば画像から読み、空なら指定の寸法・形式で空のテクスチャを作る
        //! path も寸法もなければ 1×1 の代替画像
        struct D3DTextureDesc
        {
            std::string path;                                //!< 読み込む画像ファイル
            UINT width = 0;                                  //!< 幅。path が空の時だけ見る
            UINT height = 0;                                 //!< 高さ。path が空の時だけ見る
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; //!< 画素形式。path が空の時だけ見る
            UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;     //!< 作るビュー。path が空の時だけ見る
            //! 読むビュー (SRV) の書式。DXGI_FORMAT_UNKNOWN なら format
            //! のまま。型の無い書式で作って型を付けて読む時に渡す。 path が空の時だけ見る
            DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
        };

        //! 2D テクスチャと指定されたビュー。画像読込失敗はピンクの代替画像で示す
        class D3DTexture : public NonCopyable
        {
        public:
            //! 参照先を必ず返す。画像の読み込み失敗は代替画像、空テクスチャの生成失敗は IsValid が false
            [[nodiscard]] static std::unique_ptr<D3DTexture> Create(const D3DTextureDesc& desc);

            //! @brief 既存のテクスチャリソースを包んで生成する
            //! @param[in] existing 包む対象。空なら IsValid() が false を返す
            //! @param[in] bindFlags 作るビュー。立っている物だけ SRV / RTV / DSV を作る
            [[nodiscard]] static std::unique_ptr<D3DTexture> Create(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

            ~D3DTexture();

            //! SRV / RTV / DSV のいずれかを作れているか
            [[nodiscard]] bool IsValid() const noexcept;

            [[nodiscard]] Size2D Size() const noexcept;

            //! 読み込みに失敗してピンクのフォールバックへ差し替わっているか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

            //! シェーダへ渡すリソースビューを取得する
            [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

            //! 描画先にするレンダーターゲットビューを取得する
            [[nodiscard]] ID3D11RenderTargetView* Rtv() const noexcept;

            //! 深度テストに使う深度ステンシルビューを取得する
            [[nodiscard]] ID3D11DepthStencilView* Dsv() const noexcept;

        private:
            explicit D3DTexture(const D3DTextureDesc& desc);
            D3DTexture(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

            ComPtr<ID3D11Texture2D> m_tex;
            ComPtr<ID3D11ShaderResourceView> m_srv;
            ComPtr<ID3D11RenderTargetView> m_rtv;
            ComPtr<ID3D11DepthStencilView> m_dsv;
            Size2D m_size{0, 0};
            bool m_fallback = false;
        };
    } // namespace Gfx
} // namespace NS
