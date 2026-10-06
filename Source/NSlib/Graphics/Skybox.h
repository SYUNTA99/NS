#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <string>

namespace NS::Gfx
{

    class CommandList;
    class StaticMesh;
    class Shader;
    class Buffer;
    class Pipeline;

    //! @brief スカイボックスの描画リソース
    //! @details 無限遠の背景を描くメッシュとキューブマップを持つ
    //! 画像を読み込むまではピンク一色のフォールバックを描く
    //! @warning Renderer より先に破棄すること
    class Skybox : public NS::NonCopyable
    {
    public:
        //! スカイボックス用のリソースを生成する
        [[nodiscard]] static std::unique_ptr<Skybox> Create();

        ~Skybox();

        //! @brief 指定されたパスから背景画像を読み込む
        //! @details DDS などの単一ファイルか、6 方向の画像を入れたディレクトリを指定できる
        //! @param[in] path 読み込むファイルまたはディレクトリのパス
        //! @return 成功した場合 true、それ以外の場合は false。失敗しても前の中身を保つ
        [[nodiscard]] bool LoadCubemap(std::string_view path);

        //! 立方体メッシュ・シェーダ・定数バッファ・パイプライン・代替のキューブマップが揃っているか
        [[nodiscard]] bool IsValid() const noexcept;

        //! 画像が未読み込み、または直近の読み込みに失敗したか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! @brief スカイボックスの描画コマンドを発行する
        //! @details 描画前の深度・ラスタライザ・ブレンドのステートを退避し、描いた後に戻す。無効な状態では何もしない
        //! @param[in,out] commands コマンドの発行先
        //! @param[in] viewProjNoTranslate カメラの移動成分を排除したビュー・プロジェクション行列
        //! @param[in] sampler キューブマップを拾うサンプラ。線形で端を固定する物
        //! @note 不透明なオブジェクトを描画した後、かつ半透明なオブジェクトを描画する前に呼び出すこと
        void Draw(CommandList& commands,
                  const NS::Matrix& viewProjNoTranslate,
                  ID3D11SamplerState* sampler) const noexcept;

    private:
        Skybox();

        std::unique_ptr<StaticMesh> m_cubeMesh;
        std::unique_ptr<Shader> m_vs;
        std::unique_ptr<Shader> m_ps;
        std::unique_ptr<Buffer> m_cb;
        std::unique_ptr<Pipeline> m_pipeline;
        ComPtr<ID3D11ShaderResourceView> m_cubemapSrv;
        bool m_usingFallback = true;
        bool m_valid = false;
    };

} // namespace NS::Gfx