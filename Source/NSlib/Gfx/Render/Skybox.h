#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <memory>
#include <string>
#include <string_view>

namespace NS
{
    class CameraData;
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        class D3DCommandList;
        class D3DStaticMesh;
        class D3DShader;
        class D3DBuffer;
        class D3DPipeline;
        class D3DDevice;
        class D3DTexture;
        struct BuiltinShaders;

        //! @brief 空の頂点シェーダーの定数
        //! @details skybox.vs.hlsl の cbuffer SkyboxCB と大きさを合わせる。照らす試しは shader_constants_test.cpp
        struct alignas(16) SkyboxCB
        {
            Matrix viewProj; //!< カメラの平行移動を除いたビュー射影
        };
        static_assert(sizeof(SkyboxCB) == 64, "SkyboxCB は HLSL の cbuffer SkyboxCB とバイト一致が必要");

        //! @brief 無限遠の背景の空を描く
        //! @details 立方体のメッシュとキューブマップを持つ。描く資源は、画像を指定した初回の Draw で作る
        //! 画像を読み込むまではピンク一色のフォールバックを描く
        //! @warning D3DDevice より先に破棄すること。シェーダーの持ち主が捨てた後は描画を呼ばない
        class Skybox : public NonCopyable
        {
        public:
            //! @param[in] shaders skyboxVs と skyboxPs を指すだけで持たない。使えるかは Draw の度に IsUsable で決める
            explicit Skybox(const BuiltinShaders& shaders) noexcept;
            ~Skybox();

            //! @brief 不透明の後、半透明の前に空を描く。変更した描画ステートは終了時に戻す
            //! @details cubemapPath は ContentRoot 内の .dds か、6 方向の画像を入れたディレクトリ。空なら描かない
            //! 画像の読み込みに失敗した時は前の画像を保って次の Draw で再試行し、GPU 資源の生成失敗は再試行しない
            void Draw(D3DDevice& device, const CameraData& camera, std::string_view cubemapPath) noexcept;

            //! 立方体メッシュ・定数バッファ・パイプライン・代替のキューブマップが揃っているか
            [[nodiscard]] bool IsValid() const noexcept;

            //! 画像が未読み込み、または直近の読み込みに失敗したか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

        private:
            // 描く資源を初回だけ作る
            void EnsureResources() noexcept;

            // cubemapPath が前回と違う時だけ読み直す。ContentRoot の外を指す道は読まない
            void UpdateCubemap(std::string_view cubemapPath) noexcept;

            // DDS などの単一ファイルか、6 方向の画像を入れたディレクトリを読む。失敗しても前の中身を保つ
            [[nodiscard]] bool LoadCubemap(std::string_view path);

            // viewProjNoTranslate はカメラの平行移動を除く行列、sampler は線形・端固定
            void DrawCube(D3DCommandList& commands,
                          const Matrix& viewProjNoTranslate,
                          ID3D11SamplerState* sampler) const noexcept;

            std::unique_ptr<D3DStaticMesh> m_cubeMesh;
            D3DShader* m_vs = nullptr;
            D3DShader* m_ps = nullptr;
            std::unique_ptr<D3DBuffer> m_cb;
            std::unique_ptr<D3DPipeline> m_pipeline;
            std::unique_ptr<D3DTexture> m_cubemap;
            std::string m_loadedPath;
            bool m_usingFallback = true;
            bool m_tried = false;
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
