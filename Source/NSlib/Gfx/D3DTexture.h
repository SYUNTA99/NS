#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <array>
#include <memory>
#include <string>

namespace NS
{
    namespace Gfx
    {
        /// path があれば画像から読み、空なら指定の寸法・形式で空のテクスチャを作る
        /// path も寸法もなければ 1×1 の代替画像
        struct D3DTextureDesc
        {
            std::string path;                                // 読み込む画像ファイル
            UINT width = 0;                                  // 幅。path が空の時だけ見る
            UINT height = 0;                                 // 高さ。path が空の時だけ見る
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; // 画素形式。path が空の時だけ見る
            UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;     // 作るビュー。path が空の時だけ見る
            /// 読むビュー (SRV) の書式。DXGI_FORMAT_UNKNOWN なら format
            /// のまま。型の無い書式で作って型を付けて読む時に渡す。 path が空の時だけ見る
            DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
        };

        /// 2D か立方体のテクスチャと指定されたビュー。画像読込失敗はピンクの代替画像で示す
        /// 画像ファイルから GPU のテクスチャを作るのはこの型だけ。mipmap・色空間・代替画像の決まりを 2D と立方体で共有する
        class D3DTexture : public NonCopyable
        {
        public:
            /// 参照先を必ず返す。画像の読み込み失敗は代替画像、空テクスチャの生成失敗は IsValid が false
            [[nodiscard]] static std::unique_ptr<D3DTexture> Create(const D3DTextureDesc& desc);

            /// 既存のテクスチャリソースを包んで生成する
            /// @param[in] existing 包む対象。空なら IsValid() が false を返す
            /// @param[in] bindFlags 作るビュー。立っている物だけ SRV / RTV / DSV を作る
            [[nodiscard]] static std::unique_ptr<D3DTexture> Create(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

            /// 立方体の DDS から立方体のテクスチャを作る。参照先を必ず返す
            /// 読めない時と立方体でない DDS の時は、ピンクの立方体の代替画像
            [[nodiscard]] static std::unique_ptr<D3DTexture> CreateCubeFromDds(const std::string& path);

            /// 6 枚の画像を +X・-X・+Y・-Y・+Z・-Z の面に並べた立方体のテクスチャを作る。参照先を必ず返す
            /// 各面は 2D と同じ道で読み、2D と同じ段数の mipmap を持つ
            /// 1 枚でも読めない時と、大きさ・書式・段数が揃わない時は、ピンクの立方体の代替画像
            [[nodiscard]] static std::unique_ptr<D3DTexture>
            CreateCubeFromFaces(const std::array<std::string, 6>& facePaths);

            /// ピンクの立方体の代替画像を作る。IsUsingFallback は true
            [[nodiscard]] static std::unique_ptr<D3DTexture> CreateCubeFallback();

            ~D3DTexture();

            /// SRV / RTV / DSV のいずれかを作れているか
            [[nodiscard]] bool IsValid() const noexcept;

            [[nodiscard]] Size2D Size() const noexcept;

            /// 読み込みに失敗してピンクのフォールバックへ差し替わっているか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

            /// シェーダへ渡すリソースビューを取得する
            [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

            /// 描画先にするレンダーターゲットビューを取得する
            [[nodiscard]] ID3D11RenderTargetView* Rtv() const noexcept;

            /// 深度テストに使う深度ステンシルビューを取得する
            [[nodiscard]] ID3D11DepthStencilView* Dsv() const noexcept;

        private:
            D3DTexture() noexcept = default;
            explicit D3DTexture(const D3DTextureDesc& desc);
            D3DTexture(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

            // 読めた資源を持つ。読めなければ代替画像 (cube なら立方体) へ差し替える
            void AdoptLoaded(ID3D11Device* device, ComPtr<ID3D11Resource> loaded, bool cube) noexcept;

            ComPtr<ID3D11Texture2D> m_tex;
            ComPtr<ID3D11ShaderResourceView> m_srv;
            ComPtr<ID3D11RenderTargetView> m_rtv;
            ComPtr<ID3D11DepthStencilView> m_dsv;
            Size2D m_size{0, 0};
            bool m_fallback = false;
        };

        /// RTV / DSV などのビューから裏の 2D テクスチャを取る。outDesc を渡すと記述も埋める。2D でなければ nullptr
        [[nodiscard]] ComPtr<ID3D11Texture2D> ViewTexture2D(ID3D11View* view,
                                                            D3D11_TEXTURE2D_DESC* outDesc = nullptr) noexcept;
    } // namespace Gfx
} // namespace NS
