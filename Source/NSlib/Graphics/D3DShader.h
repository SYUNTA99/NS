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
        class D3DShader : public NonCopyable
        {
        public:
            //! HLSLファイルからシェーダを生成する
            [[nodiscard]] static std::unique_ptr<D3DShader> Create(std::string_view hlslPath);

            [[nodiscard]] bool IsValid() const noexcept;

            //! 読み込みに失敗してピンクのフォールバックへ差し替わっているか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            [[nodiscard]] ShaderType Type() const noexcept;

            [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

            //! 頂点レイアウト生成用のバイナリコードを取得する
            [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

            //! @brief hlslPath のファイルを読み直し、シェーダを更新する
            //! @details 道は持たないので、作った時の道を呼び手が渡す。ファイル名のステージが作った時と違えば読まない
            //! @return 成功した場合 true、それ以外の場合は false。失敗しても前の中身を保つ
            [[nodiscard]] bool Reload(std::string_view hlslPath);

        private:
            explicit D3DShader(std::string_view hlslPath);

            [[nodiscard]] bool Compile(std::string_view hlslPath,
                                       ComPtr<ID3D11DeviceChild>& outShader,
                                       ComPtr<ID3DBlob>& outVsBytecode) const noexcept;

            ShaderType m_type = ShaderType::Unknown;
            ComPtr<ID3D11DeviceChild> m_shader;
            ComPtr<ID3DBlob> m_vsBytecode;
            bool m_fallback = false;
        };
    } // namespace Gfx
} // namespace NS
