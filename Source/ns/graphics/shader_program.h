#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;

namespace ns::graphics
{

    class Renderer;
    class ShaderProgram;

    namespace detail
    {
        /// Material 等が直接 D3D11 API に渡すために提供する typed friend accessor。
        [[nodiscard]] ID3D11VertexShader* GetVertexShader(ShaderProgram& sp) noexcept;
        [[nodiscard]] ID3D11PixelShader* GetPixelShader(ShaderProgram& sp) noexcept;
        [[nodiscard]] ID3D11InputLayout* GetInputLayout(ShaderProgram& sp) noexcept;
    } // namespace detail

    /// 公開 InputElement 用フォーマット。D3D11 / DXGI を漏らさない独自 enum。
    enum class InputElementFormat
    {
        Float2, ///< R32G32_FLOAT
        Float3, ///< R32G32B32_FLOAT
        Float4, ///< R32G32B32A32_FLOAT
        UInt32, ///< R32_UINT
    };

    /// InputLayout の 1 要素。SemanticIndex は常に 0、InputSlot 0 単一 stream 前提 ( 範囲)。
    /// Mesh::StandardInputLayout() から流用する想定 (offsetof(MeshVertex, ...) で byteOffset を埋める)。
    struct InputElement
    {
        std::string semanticName;
        InputElementFormat format = InputElementFormat::Float3;
        unsigned byteOffset = 0;
    };

    /// ShaderProgram 構築パラメータ。
    /// path が空 or 読込・コンパイル失敗時は埋込 magenta fallback HLSL に切替わり、IsUsingFallback() が true になる。
    /// fallback VS は POSITION (float3) を消費するので inputLayout に POSITION が含まれない場合は IsValid() == false。
    struct ShaderProgramDesc
    {
        std::filesystem::path vertexShaderPath;
        std::filesystem::path pixelShaderPath;
        std::string vertexEntryPoint = "VSMain";
        std::string pixelEntryPoint = "PSMain";
        std::vector<InputElement> inputLayout;
    };

    /// VS + PS + InputLayout の三位一体バンドル。
    /// ランタイム D3DCompile で .hlsl を vs_5_0 / ps_5_0 にコンパイル。
    /// VS/PS/InputLayout のいずれかで失敗すると埋込 HLSL の magenta fallback (PS 出力 RGB=(1,0,1)) に切替える。
    /// 依存: Renderer の DeviceContext を内部で保持するため、Renderer より先に破棄すること。
    class ShaderProgram
    {
    public:
        struct Impl;

        ShaderProgram(Renderer& renderer, const ShaderProgramDesc& desc);
        ~ShaderProgram();

        ShaderProgram(const ShaderProgram&) = delete;
        ShaderProgram& operator=(const ShaderProgram&) = delete;
        ShaderProgram(ShaderProgram&&) = delete;
        ShaderProgram& operator=(ShaderProgram&&) = delete;

        /// VS / PS / InputLayout が全て生成済みなら true。fallback でも true。
        [[nodiscard]] bool IsValid() const noexcept;

        /// 読込・コンパイル失敗で magenta fallback に切替わっているかを問い合わせる。
        /// デバッグ時のシェーダ欠落検知に使用。
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// VSSetShader + PSSetShader + IASetInputLayout を一括実行。
        void Bind() noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11VertexShader* detail::GetVertexShader(ShaderProgram& sp) noexcept;
        friend ID3D11PixelShader* detail::GetPixelShader(ShaderProgram& sp) noexcept;
        friend ID3D11InputLayout* detail::GetInputLayout(ShaderProgram& sp) noexcept;
    };

} // namespace ns::graphics
