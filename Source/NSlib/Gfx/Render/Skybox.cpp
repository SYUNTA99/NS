#include "NSlib/Gfx/Render/Skybox.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DCommonStates.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Core/Geometry/MeshPrimitives.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DStaticMesh.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/detail/TextureFile.h"
#include "NSlib/Windows/Filesystem.h"

#include <array>

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            // キューブマップのテクスチャファイル名
            // 素材のファイル名が示す向きは DX11 の軸から水平に 90° ずれている。継ぎ目が合う並びで置く
            constexpr std::array<const char*, 6> k_KurtFaceFileNames = {
                "space_ft.png", // 右
                "space_bk.png", // 左
                "space_up.png", // 上
                "space_dn.png", // 下
                "space_rt.png", // 奥
                "space_lf.png", // 手前
            };
        } // namespace

        Skybox::Skybox(const BuiltinShaders& shaders) noexcept : m_vs(shaders.skyboxVs), m_ps(shaders.skyboxPs)
        {
        }

        Skybox::~Skybox() = default;

        void Skybox::EnsureResources() noexcept
        {
            if (m_tried)
            {
                return;
            }
            m_tried = true;

            ID3D11Device* d3d11Device = Gpu().device;
            if (d3d11Device == nullptr)
            {
                NS_LOG_WARN(Graphics, "Skybox: device が無いため空を描かない");
                return;
            }

            MeshGeometry geom = MakeCube({0.5f, 0.5f, 0.5f});
            D3DStaticMeshDesc md{};
            md.vertices = geom.vertices.data();
            md.vertexCount = geom.vertices.size();
            md.indices = geom.indices.data();
            md.indexCount = geom.indices.size();
            m_cubeMesh = D3DStaticMesh::Create(md);
            if (!m_cubeMesh->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Skybox: cube mesh 構築失敗");
                return;
            }

            D3DBufferDesc cbDesc = MakeConstantBufferDesc(sizeof(SkyboxCB));
            m_cb = D3DBuffer::Create(cbDesc);
            if (!m_cb->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Skybox: ConstantBuffer 構築失敗");
                return;
            }

            // 立方体の内側から見るので表面をカリングし、深度は書かない
            D3DPipelineDesc pipeDesc{};
            pipeDesc.cull = CullMode::Front;
            pipeDesc.depth = DepthMode::ReadOnly;
            m_pipeline = D3DPipeline::Create(pipeDesc);
            if (!m_pipeline->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Skybox: D3DPipeline 構築失敗");
                return;
            }
            // 未ロード時でも安全に描画できるよう、初期状態として代替画像を設定しておく
            m_cubemap = D3DTexture::CreateCubeFallback();
            if (!m_cubemap->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Skybox: fallback cubemap 構築失敗");
                return;
            }

            m_usingFallback = true;
            m_valid = true;
        }

        void Skybox::Draw(D3DDevice& device, const CameraData& camera, std::string_view cubemapPath) noexcept
        {
            // cubemap を指定していないシーンは空を持たない。描く資源も作らない
            if (cubemapPath.empty())
            {
                return;
            }

            EnsureResources();
            if (!IsValid() || !device.IsValid() || !IsUsable(m_vs) || !IsUsable(m_ps))
            {
                return;
            }
            UpdateCubemap(cubemapPath);
            // 頂点シェーダーを読み直した時に、入力の並びを新しい中身に合わせる
            m_cubeMesh->CreateInputLayout(*m_vs);

            // view の平行移動成分を 0 化して camera 中心に空を固定する
            Matrix viewNoTranslate = camera.View();
            viewNoTranslate._41 = 0.0f;
            viewNoTranslate._42 = 0.0f;
            viewNoTranslate._43 = 0.0f;
            DrawCube(device.Commands(), viewNoTranslate * camera.Projection(), device.States().LinearClamp());
        }

        void Skybox::UpdateCubemap(std::string_view cubemapPath) noexcept
        {
            // 毎フレーム LoadCubemap するとファイル読み込みが常時走る
            if (cubemapPath == m_loadedPath)
            {
                return;
            }
            // ユーザー編集ファイル由来のパスを ContentRoot 配下へ閉じ込める。外を指す値は読み込まない
            const std::optional<std::string> absPath =
                OS::FileSystem::ResolveUnder(OS::FileSystem::ContentRoot(), cubemapPath);
            if (!absPath.has_value())
            {
                NS_LOG_WARN(Graphics, "Skybox: cubemap パス '{}' は ContentRoot 配下でないため読み込まない", cubemapPath);
                // 拒否はパスを直すまで変わらないので、覚えて警告の連打を止める
                m_loadedPath = cubemapPath;
            }
            else if (LoadCubemap(*absPath))
            {
                m_loadedPath = cubemapPath;
            }
            else
            {
                NS_LOG_WARN(Graphics, "Skybox: cubemap 読込失敗 ({}), 既存を維持", *absPath);
                // 失敗時は前回パスを更新しないので次フレームで再試行できる
            }
        }

        bool Skybox::LoadCubemap(std::string_view path)
        {
            std::unique_ptr<D3DTexture> loaded;
            if (detail::IsDdsExtension(path))
            {
                loaded = D3DTexture::CreateCubeFromDds(std::string(path));
            }
            else if (OS::FileSystem::IsDirectory(path))
            {
                // ディレクトリが指定された場合は、6方向の画像ファイルとして読み込みを試みる
                std::array<std::string, 6> facePaths{};
                for (std::size_t i = 0; i < facePaths.size(); ++i)
                {
                    facePaths[i] = OS::FileSystem::Combine(path, k_KurtFaceFileNames[i]);
                }
                loaded = D3DTexture::CreateCubeFromFaces(facePaths);
            }
            else
            {
                NS_LOG_ERROR(Graphics, "Skybox LoadCubemap: ディレクトリでも .dds でもないパス: {}", path);
            }

            // 代替画像に落ちた時は前の画像を保つ
            if (loaded && loaded->IsValid() && !loaded->IsUsingFallback())
            {
                m_cubemap = std::move(loaded);
                m_usingFallback = false;
                return true;
            }

            m_usingFallback = true;
            return false;
        }

        void Skybox::DrawCube(D3DCommandList& commands,
                              const Matrix& viewProjNoTranslate,
                              ID3D11SamplerState* sampler) const noexcept
        {
            if (!IsValid() || commands.Native() == nullptr)
            {
                return;
            }

            SkyboxCB cbData{};
            cbData.viewProj = viewProjNoTranslate;
            commands.UpdateSubresource(*m_cb, &cbData, sizeof(cbData));

            // 他の描画処理に影響を与えないよう、現在のステートを退避する
            ComPtr<ID3D11DepthStencilState> prevDss;
            UINT prevStencilRef = 0;
            commands->OMGetDepthStencilState(prevDss.GetAddressOf(), &prevStencilRef);

            ComPtr<ID3D11RasterizerState> prevRs;
            commands->RSGetState(prevRs.GetAddressOf());

            ComPtr<ID3D11BlendState> prevBlend;
            float prevBlendFactor[4] = {};
            UINT prevSampleMask = 0xFFFFFFFFu;
            commands->OMGetBlendState(prevBlend.GetAddressOf(), prevBlendFactor, &prevSampleMask);

            commands.SetPipeline(*m_pipeline);

            commands.VSSetShader(*m_vs);
            commands.PSSetShader(*m_ps);
            commands.VSSetConstantBuffer(*m_cb, k_DrawConstantsSlot);

            commands.PSSetShaderResource(*m_cubemap, 0);

            commands.PSSetSampler(sampler, 0);

            DrawMesh(commands, *m_cubeMesh);

            commands.UnbindPixelShaderResources(0, 1);

            commands->OMSetDepthStencilState(prevDss.Get(), prevStencilRef);
            commands->RSSetState(prevRs.Get());
            commands->OMSetBlendState(prevBlend.Get(), prevBlendFactor, prevSampleMask);
        }

        bool Skybox::IsValid() const noexcept
        {
            return m_valid;
        }

        bool Skybox::IsUsingFallback() const noexcept
        {
            return m_usingFallback;
        }
    } // namespace Gfx
} // namespace NS
