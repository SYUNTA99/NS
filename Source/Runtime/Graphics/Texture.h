#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <filesystem>

namespace NS::Graphics
{

    class Texture;

    //! ファイルからテクスチャを読み込むための初期化パラメータ
    struct TextureDesc
    {
        std::filesystem::path path;
        bool generateMipmaps = true;
        bool sRGB = false;
    };

    //! テクスチャを生成するための初期化パラメータ
    struct TextureCreateDesc
    {
        UINT width = 0;
        UINT height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT mipLevels = 1;
        UINT arraySize = 1;
        UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;
    };

    //! @brief 2D テクスチャ
    //! @details 作り方は 3 通り。画像ファイルから読む、描画先などの空のテクスチャを作る、既存リソースを包む
    //! bindFlags に応じて SRV / RTV / DSV のうち要る物だけを作る
    //! @note 画像の読み込みに失敗するとピンク一色のフォールバックへ差し替わる
    class Texture : public NS::Core::NonCopyable
    {
    public:
        //! @brief ファイルからテクスチャを生成する
        //! @return 生成に失敗しても非 null を返す。中身はフォールバックになる
        [[nodiscard]] static std::unique_ptr<Texture> Create(const TextureDesc& desc);

        //! ファイルパスを指定してテクスチャを生成する
        [[nodiscard]] static std::unique_ptr<Texture> Create(const std::filesystem::path& path);

        //! パラメータを指定して空のテクスチャを生成する
        [[nodiscard]] static std::unique_ptr<Texture> Create(const TextureCreateDesc& desc);

        //! 既存のテクスチャリソースをラップして生成する
        [[nodiscard]] static std::unique_ptr<Texture> Create(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

        ~Texture();

        //! SRV / RTV / DSV のいずれかを作れているか
        [[nodiscard]] bool IsValid() const noexcept;

        //! テクスチャの幅と高さを取得する
        [[nodiscard]] NS::Core::Size2D Size() const noexcept;

        //! 読み込みに失敗してピンクのフォールバックへ差し替わっているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! DX11 のテクスチャを取得する
        [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

        //! シェーダへ渡すリソースビューを取得する
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

        //! 描画先にするレンダーターゲットビューを取得する
        [[nodiscard]] ID3D11RenderTargetView* Rtv() const noexcept;

        //! 深度テストに使う深度ステンシルビューを取得する
        [[nodiscard]] ID3D11DepthStencilView* Dsv() const noexcept;

    private:
        explicit Texture(const TextureDesc& desc);
        explicit Texture(const std::filesystem::path& path);
        explicit Texture(const TextureCreateDesc& desc);
        Texture(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

        ComPtr<ID3D11Texture2D> m_tex;
        ComPtr<ID3D11ShaderResourceView> m_srv;
        ComPtr<ID3D11RenderTargetView> m_rtv;
        ComPtr<ID3D11DepthStencilView> m_dsv;
        NS::Core::Size2D m_size{0, 0};
        bool m_fallback = false;
    };

} // namespace NS::Graphics