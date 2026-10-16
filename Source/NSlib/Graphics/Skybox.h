#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <string>

namespace NS
{
    namespace Gfx
    {
        class D3DCommandList;
        class D3DStaticMesh;
        class D3DShader;
        class D3DBuffer;
        class D3DPipeline;
        struct BuiltinShaders;

        //! @brief スカイボックスの描画リソース
        //! @details 無限遠の背景を描くメッシュとキューブマップを持つ
        //! 画像を読み込むまではピンク一色のフォールバックを描く
        //! @warning Renderer より先に破棄すること。シェーダーの持ち主が捨てた後は描画を呼ばない
        class Skybox : public NonCopyable
        {
        public:
            //! @brief スカイボックス用のリソースを生成する
            //! @param[in] shaders skyboxVs と skyboxPs を指すだけで持たない。どちらかが nullptr なら無効になる
            [[nodiscard]] static std::unique_ptr<Skybox> Create(const BuiltinShaders& shaders);

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

            //! 不透明の後、半透明の前に描く。変更した描画ステートは終了時に戻す
            //! viewProjNoTranslate はカメラの平行移動を除く行列、sampler は線形・端固定
            void Draw(D3DCommandList& commands,
                      const Matrix& viewProjNoTranslate,
                      ID3D11SamplerState* sampler) const noexcept;

        private:
            explicit Skybox(const BuiltinShaders& shaders);

            std::unique_ptr<D3DStaticMesh> m_cubeMesh;
            D3DShader* m_vs = nullptr;
            D3DShader* m_ps = nullptr;
            std::unique_ptr<D3DBuffer> m_cb;
            std::unique_ptr<D3DPipeline> m_pipeline;
            ComPtr<ID3D11ShaderResourceView> m_cubemapSrv;
            bool m_usingFallback = true;
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
