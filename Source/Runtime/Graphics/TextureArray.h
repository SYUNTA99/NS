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

    //! @brief 複数の2Dテクスチャを1つのリソースにまとめた配列テクスチャ。
    //! @details シェーダ内でインデックスを指定することで、任意のテクスチャを切り替えて描画できる。
    //! @note 配列内の全テクスチャは幅・高さ・フォーマットが完全に一致している必要がある。
    //! 読み込みに失敗した場合は、エラーを示す代替画像（ピンク色）に差し替えられる。
    class TextureArray : public NS::Core::NonCopyable
    {
    public:
        //! 保持できるテクスチャの最大枚数。指定数を超えた分は無視される
        static constexpr std::uint16_t k_TotalSlices = 64;

        //! @brief テクスチャ配列を生成する
        //! @param desc 初期化パラメータ
        //! @return 生成失敗時も非nullのインスタンスを返す
        [[nodiscard]] static std::unique_ptr<TextureArray> Create(const TextureArrayDesc& desc);

        ~TextureArray();

        //! 描画リソースとして正常に利用可能であればtrue
        [[nodiscard]] bool IsValid() const noexcept;

        //! 画像の読み込みに失敗し、代替表示（ピンク色）が適用されているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! 実際に読み込まれたテクスチャの枚数
        [[nodiscard]] std::uint16_t SliceCount() const noexcept;

        //! 内部のグラフィックスAPI用テクスチャオブジェクトを取得する
        [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

        //! シェーダに渡すためのリソースビューを取得する
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

    private:
        explicit TextureArray(const TextureArrayDesc& desc);

        ComPtr<ID3D11Texture2D> m_arrayTexture;
        ComPtr<ID3D11ShaderResourceView> m_srv;
        std::uint16_t m_sliceCount = 0;
        bool m_fallback = false;
    };

} // namespace NS::Graphics
