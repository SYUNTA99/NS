#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <span>
#include <string>

namespace NS
{
    namespace Gfx
    {
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

        //! ファイル名からステージを判定して実行時にコンパイルするシェーダ
        //! 頂点・ピクセルシェーダの読込失敗はピンクの代替表示で示す
        class Shader : public NonCopyable
        {
        public:
            //! HLSLファイルからシェーダを生成する
            [[nodiscard]] static std::unique_ptr<Shader> Create(std::string_view hlslPath);

            //! @brief ContentRoot の Shaders ディレクトリにある HLSL ファイルからシェーダを生成する
            //! @param[in] fileName Shaders ディレクトリ相対のファイル名 (例 "fade.vs.hlsl")
            [[nodiscard]] static std::unique_ptr<Shader> CreateBuiltin(std::string_view fileName);

            [[nodiscard]] bool IsValid() const noexcept;

            //! 読み込みに失敗してピンクのフォールバックへ差し替わっているか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            [[nodiscard]] ShaderType Type() const noexcept;

            [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

            //! 頂点レイアウト生成用のバイナリコードを取得する
            [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

            //! @brief ファイルを再読み込みし、シェーダを更新する
            //! @return 成功した場合 true、それ以外の場合は false。失敗しても前の中身を保つ
            [[nodiscard]] bool Reload();

        private:
            explicit Shader(std::string_view hlslPath);

            //! コンパイルして、成功した時だけシェーダを作る
            [[nodiscard]] bool Compile(ComPtr<ID3D11DeviceChild>& outShader,
                                       ComPtr<ID3DBlob>& outVsBytecode) const noexcept;

            std::string m_sourcePath;
            ShaderType m_type = ShaderType::Unknown;
            ComPtr<ID3D11DeviceChild> m_shader;
            ComPtr<ID3DBlob> m_vsBytecode;
            bool m_fallback = false;
        };
    } // namespace Gfx
} // namespace NS
