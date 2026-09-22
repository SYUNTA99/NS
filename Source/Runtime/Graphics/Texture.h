#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <string>

namespace NS::Gfx
{

    class Texture;

    //! @brief テクスチャの作り方
    //! @details path が空でなければ画像ファイルから読む。大きさと画素形式はファイルが決める
    //! path が空なら width / height / format / bindFlags で中身の無いテクスチャを作る
    //! path も大きさも無ければ 1x1 のフォールバックになる
    struct TextureDesc
    {
        std::string path;                                //!< 読み込む画像ファイル
        UINT width = 0;                                  //!< 幅。path が空の時だけ見る
        UINT height = 0;                                 //!< 高さ。path が空の時だけ見る
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; //!< 画素形式。path が空の時だけ見る
        UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;     //!< 作るビュー。path が空の時だけ見る
    };

    //! @brief 2D テクスチャ
    //! @details 作り方は 2 通り。TextureDesc を渡す、既存リソースを包む
    //! bindFlags に応じて SRV / RTV / DSV のうち要る物だけを作る
    //! @note 画像の読み込みに失敗するとピンク一色のフォールバックへ差し替わる
    class Texture : public NS::Core::NonCopyable
    {
    public:
        //! @brief テクスチャを生成する
        //! @param[in] desc 読み込む画像、または作る中身の無いテクスチャの形
        //! @return 画像の読み込みに失敗しても非 null を返す。中身はフォールバックになる
        //! @details 中身の無いテクスチャの生成に失敗した場合はフォールバックへ差し替えず IsValid() が false を返す
        [[nodiscard]] static std::unique_ptr<Texture> Create(const TextureDesc& desc);

        //! @brief 既存のテクスチャリソースを包んで生成する
        //! @param[in] existing 包む対象。空なら IsValid() が false を返す
        //! @param[in] bindFlags 作るビュー。立っている物だけ SRV / RTV / DSV を作る
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
        Texture(ComPtr<ID3D11Texture2D> existing, UINT bindFlags);

        ComPtr<ID3D11Texture2D> m_tex;
        ComPtr<ID3D11ShaderResourceView> m_srv;
        ComPtr<ID3D11RenderTargetView> m_rtv;
        ComPtr<ID3D11DepthStencilView> m_dsv;
        NS::Core::Size2D m_size{0, 0};
        bool m_fallback = false;
    };

} // namespace NS::Gfx