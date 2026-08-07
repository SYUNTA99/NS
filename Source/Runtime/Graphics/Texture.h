#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Core/Math.h"

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

    //! @brief 2Dテクスチャを管理するクラス。
    //! @details
    //! 画像ファイルからの読み込み、空のテクスチャ（描画先など）の生成、既存リソースのラップという3つの役割を担う。
    //! 指定された用途に応じて、必要なリソースビュー（SRV / RTV / DSV）が自動的に構築される。
    //! @note 画像の読み込みに失敗した場合は、エラーを示す代替画像（ピンク色）が適用される。
    class Texture : public NS::Core::NonCopyable
    {
    public:
        //! @brief ファイルからテクスチャを生成する。
        //! @return 生成失敗時も非nullのインスタンスを返す（代替画像が適用される）
        [[nodiscard]] static std::unique_ptr<Texture> Create(const TextureDesc& desc);

        //! ファイルパスを指定してテクスチャを生成する
        [[nodiscard]] static std::unique_ptr<Texture> Create(const std::filesystem::path& path);

        //! パラメータを指定して空のテクスチャを生成する
        [[nodiscard]] static std::unique_ptr<Texture> Create(const TextureCreateDesc& desc);

        //! 既存のテクスチャリソースをラップして生成する
        [[nodiscard]] static std::unique_ptr<Texture> Create(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

        ~Texture();

        [[nodiscard]] bool IsValid() const noexcept;

        //! テクスチャの幅と高さを取得する
        [[nodiscard]] NS::Core::Size2D Size() const noexcept;

        //! 画像の読み込みに失敗し、代替表示（ピンク色）が適用されているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! 内部のグラフィックスAPI用テクスチャオブジェクトを取得する
        [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

        //! シェーダに渡すためのリソースビューを取得する
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

        //! 描画先として使用するためのレンダーターゲットビューを取得する
        [[nodiscard]] ID3D11RenderTargetView* Rtv() const noexcept;

        //! 深度テストに使用するための深度ステンシルビューを取得する
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