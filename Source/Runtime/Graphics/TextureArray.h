#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <filesystem>

namespace NS::Graphics
{

    class TextureArray;

    //! テクスチャ配列の初期化パラメータ
    struct TextureArrayDesc
    {
        std::vector<std::filesystem::path> slicePaths;
        bool generateMipmaps = true;
        bool sRGB = false;
    };

    //! @brief 複数の 2D テクスチャを 1 つにまとめた配列テクスチャ
    //! @details シェーダで番号を指定すると、描画のたびに切り替えられる
    //! @note 全テクスチャの幅・高さ・フォーマットが一致していること
    //! 読み込みに失敗するとピンク一色のフォールバックへ差し替わる
    class TextureArray : public NS::Core::NonCopyable
    {
    public:
        //! 保持できるテクスチャの最大枚数。指定数を超えた分は無視される
        static constexpr std::uint16_t k_TotalSlices = 64;

        //! @brief テクスチャ配列を生成する
        //! @param[in] desc 初期化パラメータ
        //! @return 生成失敗時も非nullのインスタンスを返す
        [[nodiscard]] static std::unique_ptr<TextureArray> Create(const TextureArrayDesc& desc);

        ~TextureArray();

        //! 描画リソースとして正常に利用可能であればtrue
        [[nodiscard]] bool IsValid() const noexcept;

        //! 読み込みに失敗してピンクのフォールバックへ差し替わっているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! 実際に読み込まれたテクスチャの枚数
        [[nodiscard]] std::uint16_t SliceCount() const noexcept;

        //! DX11 のテクスチャを取得する
        [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

        //! シェーダへ渡すリソースビューを取得する
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

    private:
        explicit TextureArray(const TextureArrayDesc& desc);

        ComPtr<ID3D11Texture2D> m_arrayTexture;
        ComPtr<ID3D11ShaderResourceView> m_srv;
        std::uint16_t m_sliceCount = 0;
        bool m_fallback = false;
    };

} // namespace NS::Graphics
