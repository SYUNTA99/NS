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

    //! @brief 描画処理を行う単一ステージのシェーダ。
    //! @details ファイルパスからステージ（頂点・ピクセル等）を自動判定し、実行時にコンパイルする。
    //! 頂点・ピクセルシェーダで読み込みに失敗した場合は、エラーを視覚化するための代替表示（ピンク色）に自動切り替えする。
    //! コンパイル後の内部オブジェクトは、描画コマンドの発行用クラスがステージに応じて適宜使用する
    class Shader : public NS::Core::NonCopyable
    {
    public:
        //! HLSLファイルからシェーダを生成する
        [[nodiscard]] static std::unique_ptr<Shader> Create(const std::filesystem::path& hlslPath);

        [[nodiscard]] bool IsValid() const noexcept;

        //! 読み込み失敗により代替表示（ピンク色）が有効になっているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! シェーダの種類を返す
        [[nodiscard]] ShaderType Type() const noexcept;

        //! 内部のグラフィックスAPI用オブジェクトを取得する
        [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

        //! 頂点レイアウト生成用のバイナリコードを取得する
        [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

        //! @brief ファイルを再読み込みし、シェーダを更新する
        //! @return 成功時はtrue。失敗時は旧状態を保持する
        [[nodiscard]] bool Reload();

    private:
        explicit Shader(const std::filesystem::path& hlslPath);

        //! コンパイル処理、成功時のみシェーダオブジェクトを生成する
        [[nodiscard]] bool Compile(ComPtr<ID3D11DeviceChild>& outShader,
                                   ComPtr<ID3DBlob>& outVsBytecode) const noexcept;

        std::filesystem::path m_sourcePath;
        ShaderType m_type = ShaderType::Unknown;
        ComPtr<ID3D11DeviceChild> m_shader;
        ComPtr<ID3DBlob> m_vsBytecode;
        bool m_fallback = false;
    };

} // namespace NS::Graphics
