#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Math/Math.h"

#include <filesystem>

namespace NS::Graphics
{

    class Renderer;
    class Mesh;
    class StaticMesh;
    class Shader;
    class Buffer;
    class Pipeline;

    //! @brief 空間の背景（スカイボックス）用の描画リソースを管理するクラス。
    //! @details 無限遠の背景を表現するためのメッシュやテクスチャデータを保持する
    //! 。読み込みに失敗した場合は、エラーを示す代替画像（ピンク色）が適用される。
    //! @warning 破棄順序のバグを防ぐため、描画システム（Renderer等）よりも先に破棄されるようにすること。
    class Skybox : public NS::Core::NonCopyable
    {
    public:
        //! スカイボックス用のリソースを生成する
        [[nodiscard]] static std::unique_ptr<Skybox> Create();

        ~Skybox();

        //! @brief 指定されたパスから背景画像を読み込む
        //! @details 単一のファイル（DDS等）、または6方向の画像が含まれるディレクトリを指定できる
        //! @param path 読み込むファイルまたはディレクトリのパス
        //! @return 成功時はtrue。失敗時は旧状態（または代替画像）を維持してfalseを返す
        [[nodiscard]] bool LoadCubemap(const std::filesystem::path& path);

        [[nodiscard]] bool IsValid() const noexcept;

        //! 画像が未読み込み、または読み込みに失敗し、代替表示が適用されているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! シェーダに渡すためのキューブマップのリソースビューを取得する
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
    //! @param renderer コマンドを発行する描画システム
    //! @param skybox 描画リソースを持つスカイボックス
    //! @param viewProjNoTranslate カメラの移動成分を排除したビュー・プロジェクション行列
    //! @note 描画順序の仕様上、不透明なオブジェクトを描画した後、かつ半透明なオブジェクトを描画する前に呼び出すこと
    void IssueSkybox(Renderer& renderer, const Skybox& skybox, const NS::Math::Matrix& viewProjNoTranslate) noexcept;

} // namespace NS::Graphics