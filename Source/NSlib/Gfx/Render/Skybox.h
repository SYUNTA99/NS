#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <array>
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

        //! skybox.vs.hlsl の cbuffer SkyboxCB とレイアウトを一致させる
        struct alignas(16) SkyboxCB
        {
            Matrix viewProj; // カメラの平行移動成分を除いたビュー射影行列
        };
        static_assert(sizeof(SkyboxCB) == 64, "SkyboxCB は HLSL の cbuffer SkyboxCB とバイト一致が必要");

        //! キューブマップによるスカイボックス描画。D3DDevice より先に破棄し、shaders の破棄後は描画を呼ばないこと
        class Skybox : public NonCopyable
        {
        public:
            //! shaders は非所有で参照のみ保持する
            explicit Skybox(const BuiltinShaders& shaders) noexcept;
            ~Skybox();

            //! cubemapPath は ContentRoot 内の .dds または 6 面画像ディレクトリ (空なら描画しない)
            //! 変更した描画ステートは終了時に復元する
            void Draw(D3DDevice& device, const CameraData& camera, std::string_view cubemapPath) noexcept;

            [[nodiscard]] bool IsValid() const noexcept;

            //! キューブマップが未読み込み、または直近の読み込みに失敗してフォールバック中か
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            //! ディレクトリ指定時に読み込む 6 面のファイル名 (+X, -X, +Y, -Y, +Z, -Z 順)
            [[nodiscard]] static const std::array<std::string_view, 6>& FaceFileNames() noexcept;

        private:
            void EnsureResources() noexcept;

            void UpdateCubemap(std::string_view cubemapPath) noexcept;

            [[nodiscard]] bool LoadCubemap(std::string_view path);

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
