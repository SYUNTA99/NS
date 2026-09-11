#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <filesystem>

namespace NS::Graphics
{

    class Renderer;
    class Mesh;
    class StaticMesh;
    class Shader;
    class Buffer;
    class Pipeline;

    //! @brief スカイボックスの描画リソース
    //! @details 無限遠の背景を描くメッシュとキューブマップを持つ
    //! 画像を読み込むまではピンク一色のフォールバックを描く
    //! @warning Renderer より先に破棄すること
    class Skybox : public NS::Core::NonCopyable
    {
    public:
        //! スカイボックス用のリソースを生成する
        [[nodiscard]] static std::unique_ptr<Skybox> Create();

        ~Skybox();

        //! @brief 指定されたパスから背景画像を読み込む
        //! @details DDS などの単一ファイルか、6 方向の画像を入れたディレクトリを指定できる
        //! @param[in] path 読み込むファイルまたはディレクトリのパス
        //! @return 成功した場合 true、それ以外の場合は false。失敗しても前の中身を保つ
        [[nodiscard]] bool LoadCubemap(const std::filesystem::path& path);

        //! 立方体メッシュ・シェーダ・定数バッファ・サンプラー・キューブマップが揃っているか
        [[nodiscard]] bool IsValid() const noexcept;

        //! 画像が未読み込み、または直近の読み込みに失敗したか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! シェーダへ渡すキューブマップのリソースビューを取得する
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

        //! スカイボックス描画用のパイプライン設定を取得する
        [[nodiscard]] const Pipeline* RenderPipeline() const noexcept;

        [[nodiscard]] const Buffer* ConstantBuffer() const noexcept; //!< 定数バッファを返す
        [[nodiscard]] const Shader* VertexShader() const noexcept;   //!< 頂点シェーダを返す
        [[nodiscard]] const Shader* PixelShader() const noexcept;    //!< ピクセルシェーダを返す
        [[nodiscard]] ID3D11SamplerState* Sampler() const noexcept;  //!< キューブマップのサンプラを返す
        [[nodiscard]] const Mesh* CubeMesh() const noexcept;         //!< 背景を貼る立方体メッシュを返す

    private:
        Skybox();

        std::unique_ptr<StaticMesh> m_cubeMesh;
        std::unique_ptr<Shader> m_vs;
        std::unique_ptr<Shader> m_ps;
        std::unique_ptr<Buffer> m_cb;
        ComPtr<ID3D11SamplerState> m_sampler;
        std::unique_ptr<Pipeline> m_pipeline;
        ComPtr<ID3D11ShaderResourceView> m_cubemapSrv;
        bool m_usingFallback = true;
        bool m_valid = false;
    };

    //! @brief 指定されたスカイボックスの描画コマンドを発行する
    //! @param[in,out] renderer コマンドを発行する描画システム
    //! @param[in] skybox 描画リソースを持つスカイボックス
    //! @param[in] viewProjNoTranslate カメラの移動成分を排除したビュー・プロジェクション行列
    //! @note 不透明なオブジェクトを描画した後、かつ半透明なオブジェクトを描画する前に呼び出すこと
    void IssueSkybox(Renderer& renderer, const Skybox& skybox, const NS::Core::Matrix& viewProjNoTranslate) noexcept;

} // namespace NS::Graphics