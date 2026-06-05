#pragma once

/// @file Shader.h
/// @brief NS::Graphics::Shader — VS + PS + InputLayout の三位一体バンドル
///
/// @details ランタイム `D3DCompile` で `.hlsl` を `vs_5_0` / `ps_5_0` にコンパイル
/// VS / PS / InputLayout のいずれかで失敗すると埋込 HLSL の magenta fallback
/// (PS 出力 RGB=(1,0,1)) に切替え、 `IsUsingFallback()` が true になる
/// `InputElementFormat` は D3D11 / DXGI を漏らさない独自 enum、 `StaticMesh::StandardInputLayout()`
/// から流用する想定。 依存: Renderer の DeviceContext を内部で保持するため Renderer より
/// 先に破棄すること

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;

namespace NS::Graphics
{

    class Renderer;
    class Shader;

    namespace detail
    {
        /// Material 等が直接 D3D11 API に渡すために提供する typed friend accessor
        [[nodiscard]] ID3D11VertexShader* GetVertexShader(Shader& sp) noexcept;
        [[nodiscard]] ID3D11PixelShader* GetPixelShader(Shader& sp) noexcept;
        [[nodiscard]] ID3D11InputLayout* GetInputLayout(Shader& sp) noexcept;
    } // namespace detail

    /// 公開 InputElement 用フォーマット。D3D11 / DXGI を漏らさない独自 enum
    enum class InputElementFormat
    {
        Float2, ///< R32G32_FLOAT
        Float3, ///< R32G32B32_FLOAT
        Float4, ///< R32G32B32A32_FLOAT
        UInt32, ///< R32_UINT
        UInt4,  ///< R32G32B32A32_UINT (4 ボーン index)
    };

    /// InputLayout の 1 要素。SemanticIndex は常に 0、InputSlot 0 単一 stream 前提
    /// StaticMesh::StandardInputLayout() から流用する想定 (offsetof(StaticVertex, ...) で byteOffset を埋める)
    struct InputElement
    {
        std::string semanticName;
        InputElementFormat format = InputElementFormat::Float3;
        unsigned byteOffset = 0;
    };

    /// Shader 構築パラメータ
    /// path が空 or 読込・コンパイル失敗時は埋込 magenta fallback HLSL に切替わり、IsUsingFallback() が true になる
    /// fallback VS は POSITION (float3) を消費するので inputLayout に POSITION が含まれない場合は IsValid() == false
    struct ShaderDesc
    {
        std::filesystem::path vertexShaderPath;
        std::filesystem::path pixelShaderPath;
        std::string vertexEntryPoint = "VSMain";
        std::string pixelEntryPoint = "PSMain";
        std::vector<InputElement> inputLayout;
    };

    /// VS + PS + InputLayout の三位一体バンドル
    /// ランタイム D3DCompile で .hlsl を vs_5_0 / ps_5_0 にコンパイル
    /// VS/PS/InputLayout のいずれかで失敗すると埋込 HLSL の magenta fallback (PS 出力 RGB=(1,0,1)) に切替える
    /// 依存: Renderer の DeviceContext を内部で保持するため、Renderer より先に破棄すること
    class Shader
    {
    public:
        struct Impl;

        Shader(Renderer& renderer, const ShaderDesc& desc);
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&) = delete;
        Shader& operator=(Shader&&) = delete;

        /// VS / PS / InputLayout が全て生成済みなら true。fallback でも true
        [[nodiscard]] bool IsValid() const noexcept;

        /// 読込・コンパイル失敗で magenta fallback に切替わっているかを問い合わせる
        /// デバッグ時のシェーダ欠落検知に使用
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// VSSetShader + PSSetShader + IASetInputLayout を一括実行
        void Bind() noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11VertexShader* detail::GetVertexShader(Shader& sp) noexcept;
        friend ID3D11PixelShader* detail::GetPixelShader(Shader& sp) noexcept;
        friend ID3D11InputLayout* detail::GetInputLayout(Shader& sp) noexcept;
    };

} // namespace NS::Graphics
