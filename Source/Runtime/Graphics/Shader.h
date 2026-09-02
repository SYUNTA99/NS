#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <filesystem>
#include <span>

namespace NS::Graphics
{
    class Shader;

    //! シェーダのパイプラインステージ種別
    enum class ShaderType
    {
        Unknown,
        Vertex,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute,
    };

    namespace detail
    {
        //! 頂点シェーダのバイナリコードを取得する
        [[nodiscard]] std::span<const std::byte> GetVertexShaderBytecode(const Shader& shader) noexcept;
    } // namespace detail

    //! @brief シェーダ 1 本
    //! @details 頂点・ピクセルなどの種類をファイル名から決め、実行時にコンパイルする
    //! 頂点・ピクセルは読み込みに失敗するとピンク一色のフォールバックへ差し替わり、画面で失敗が見える
    //! コンパイル後のシェーダオブジェクトは CommandList がステージ別のバインドで使う
    class Shader : public NS::Core::NonCopyable
    {
    public:
        //! HLSLファイルからシェーダを生成する
        [[nodiscard]] static std::unique_ptr<Shader> Create(const std::filesystem::path& hlslPath);

        [[nodiscard]] bool IsValid() const noexcept;

        //! 読み込みに失敗してピンクのフォールバックへ差し替わっているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! シェーダの種類を返す
        [[nodiscard]] ShaderType Type() const noexcept;

        //! DX11 のシェーダを取得する
        [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

        //! 頂点レイアウト生成用のバイナリコードを取得する
        [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

        //! @brief ファイルを再読み込みし、シェーダを更新する
        //! @return 成功した場合 true、それ以外の場合は false。失敗しても前の中身を保つ
        [[nodiscard]] bool Reload();

    private:
        explicit Shader(const std::filesystem::path& hlslPath);

        //! コンパイルして、成功した時だけシェーダを作る
        [[nodiscard]] bool Compile(ComPtr<ID3D11DeviceChild>& outShader,
                                   ComPtr<ID3DBlob>& outVsBytecode) const noexcept;

        std::filesystem::path m_sourcePath;
        ShaderType m_type = ShaderType::Unknown;
        ComPtr<ID3D11DeviceChild> m_shader;
        ComPtr<ID3DBlob> m_vsBytecode;
        bool m_fallback = false;
    };

} // namespace NS::Graphics
