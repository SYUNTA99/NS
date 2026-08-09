#include "Runtime/Graphics/Skybox.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/CommandList.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/MeshPrimitives.h"
#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Graphics/Shader.h"
#include "Runtime/Graphics/StaticMesh.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <algorithm>

namespace NS::Graphics
{

    namespace
    {
        // シェーダに渡す定数データ（64バイト境界に配置する）
        struct alignas(16) SkyboxCB
        {
            NS::Core::Matrix viewProj;
        };
        static_assert(sizeof(SkyboxCB) == 64, "SkyboxCB は HLSL 側 cbuffer (b0) と byte 一致が必要");

        // キューブマップのテクスチャファイル名
        // 素材の ft/bk/lf/rt 命名は D3D の軸から水平 90° ずれている。継ぎ目が合う並びで置く
        constexpr std::array<const char*, 6> k_KurtFaceFileNames = {
            "space_ft.png", // 右
            "space_bk.png", // 左
            "space_up.png", // 上
            "space_dn.png", // 下
            "space_rt.png", // 奥
            "space_lf.png", // 手前
        };

        [[nodiscard]] bool IsDdsExtension(const std::filesystem::path& path)
        {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext == ".dds";
        }

        // 読み込みに失敗した場合の代替キューブマップ（ピンク色）を生成する
        bool CreateMagentaCubemapFallback(ID3D11Device* device, ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            if (device == nullptr)
                return false;

            const std::uint8_t magenta[4] = {0xFF, 0x00, 0xFF, 0xFF};

            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1;
            td.Height = 1;
            td.MipLevels = 1;
            td.ArraySize = 6;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

            std::array<D3D11_SUBRESOURCE_DATA, 6> srd{};
            for (auto& s : srd)
            {
                s.pSysMem = magenta;
                s.SysMemPitch = sizeof(magenta);
            }

            ComPtr<ID3D11Texture2D> tex;
            HRESULT hr = device->CreateTexture2D(&td, srd.data(), tex.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    Graphics, "Skybox fallback cubemap CreateTexture2D 失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = td.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = 1;

            hr = device->CreateShaderResourceView(tex.Get(), &srvDesc, outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    Graphics, "Skybox fallback cubemap CreateSRV 失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        bool LoadDdsCubemap(ID3D11Device* device,
                            const std::filesystem::path& path,
                            ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            ComPtr<ID3D11Resource> resource;
            const HRESULT hr = DirectX::CreateDDSTextureFromFileEx(device,
                                                                   path.wstring().c_str(),
                                                                   0,
                                                                   D3D11_USAGE_IMMUTABLE,
                                                                   D3D11_BIND_SHADER_RESOURCE,
                                                                   0,
                                                                   D3D11_RESOURCE_MISC_TEXTURECUBE,
                                                                   DirectX::DDS_LOADER_DEFAULT,
                                                                   resource.GetAddressOf(),
                                                                   outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    Graphics, "Skybox .dds ロード失敗: {} (hr=0x{:08X})", path.string(), static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        // 6枚の画像ファイルを読み込み、1つのキューブマップとして結合する
        bool LoadSixFacePngCubemap(ID3D11Device* device,
                                   ID3D11DeviceContext* context,
                                   const std::filesystem::path& dir,
                                   ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            std::array<ComPtr<ID3D11Texture2D>, 6> faceTextures{};
            int faceWidth = 0;
            int faceHeight = 0;
            DXGI_FORMAT faceFormat = DXGI_FORMAT_UNKNOWN;

            for (std::size_t i = 0; i < 6; ++i)
            {
                const std::filesystem::path facePath = dir / k_KurtFaceFileNames[i];
                if (!::NS::Core::FileSystem::Exists(facePath))
                {
                    NS_LOG_ERROR(Graphics, "Skybox 6-face: {} が見つからない", facePath.string());
                    return false;
                }

                auto bytesOpt = ::NS::Core::FileSystem::ReadAllBytes(facePath);
                if (!bytesOpt.has_value())
                {
                    NS_LOG_ERROR(Graphics, "Skybox 6-face: {} 読込失敗", facePath.string());
                    return false;
                }
                const auto& bytes = bytesOpt.value();

                ComPtr<ID3D11Resource> resource;
                ComPtr<ID3D11ShaderResourceView> tmpSrv;

                const HRESULT hr =
                    DirectX::CreateWICTextureFromMemoryEx(device,
                                                          nullptr,
                                                          reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                          bytes.size(),
                                                          0u,
                                                          D3D11_USAGE_DEFAULT,
                                                          D3D11_BIND_SHADER_RESOURCE,
                                                          0u,
                                                          0u,
                                                          DirectX::WIC_LOADER_IGNORE_SRGB,
                                                          resource.GetAddressOf(),
                                                          tmpSrv.GetAddressOf());
                if (FAILED(hr) || !resource)
                {
                    NS_LOG_ERROR(Graphics,
                                 "Skybox 6-face: WIC 読込失敗 {} (hr=0x{:08X})",
                                 facePath.string(),
                                 static_cast<unsigned>(hr));
                    return false;
                }

                ComPtr<ID3D11Texture2D> tex2d;
                if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex2d.GetAddressOf()))) || !tex2d)
                {
                    NS_LOG_ERROR(Graphics, "Skybox 6-face: ID3D11Texture2D へ QI 失敗 {}", facePath.string());
                    return false;
                }

                D3D11_TEXTURE2D_DESC d{};
                tex2d->GetDesc(&d);
                if (i == 0)
                {
                    faceWidth = static_cast<int>(d.Width);
                    faceHeight = static_cast<int>(d.Height);
                    faceFormat = d.Format;
                }
                else if (static_cast<int>(d.Width) != faceWidth || static_cast<int>(d.Height) != faceHeight ||
                         d.Format != faceFormat)
                {
                    NS_LOG_ERROR(Graphics,
                                 "Skybox 6-face: face {} の解像度/フォーマット不一致 ({}x{}, fmt={})",
                                 facePath.string(),
                                 static_cast<int>(d.Width),
                                 static_cast<int>(d.Height),
                                 static_cast<int>(d.Format));
                    return false;
                }
                faceTextures[i] = std::move(tex2d);
            }

            D3D11_TEXTURE2D_DESC cubeDesc{};
            cubeDesc.Width = static_cast<UINT>(faceWidth);
            cubeDesc.Height = static_cast<UINT>(faceHeight);
            cubeDesc.MipLevels = 1;
            cubeDesc.ArraySize = 6;
            cubeDesc.Format = faceFormat;
            cubeDesc.SampleDesc.Count = 1;
            cubeDesc.Usage = D3D11_USAGE_DEFAULT;
            cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

            ComPtr<ID3D11Texture2D> cubeTex;
            HRESULT hr = device->CreateTexture2D(&cubeDesc, nullptr, cubeTex.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics,
                             "Skybox 6-face: cubemap 本体の CreateTexture2D 失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
                return false;
            }

            for (UINT face = 0; face < 6; ++face)
            {
                const UINT dstSub = D3D11CalcSubresource(0, face, 1);
                context->CopySubresourceRegion(cubeTex.Get(), dstSub, 0, 0, 0, faceTextures[face].Get(), 0, nullptr);
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = faceFormat;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = 1;

            hr = device->CreateShaderResourceView(cubeTex.Get(), &srvDesc, outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "Skybox 6-face: cubemap SRV 作成失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        bool CreateSkyboxSampler(ID3D11Device* device, ComPtr<ID3D11SamplerState>& outSampler) noexcept
        {
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

            const HRESULT hr = device->CreateSamplerState(&sd, outSampler.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "Skybox SamplerState 作成失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    std::unique_ptr<Skybox> Skybox::Create()
    {
        return std::unique_ptr<Skybox>(new Skybox());
    }

    Skybox::Skybox()
    {
        auto* device = Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(Graphics, "Skybox: グローバル Device が無効");
            return;
        }

        // スカイボックス用の立方体メッシュを生成する
        auto geom = MakeCube({0.5f, 0.5f, 0.5f});
        MeshDesc md{};
        md.vertices = geom.vertices.data();
        md.vertexCount = geom.vertices.size();
        md.indices = geom.indices.data();
        md.indexCount = geom.indices.size();
        m_cubeMesh = StaticMesh::Create(md);
        if (!m_cubeMesh->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Skybox: cube mesh 構築失敗");
            return;
        }

        // 専用シェーダを読み込む
        const auto exeDir = ::NS::Core::FileSystem::ContentRoot();
        m_vs = Shader::Create(exeDir / "Shaders" / "skybox.vs.hlsl");
        m_ps = Shader::Create(exeDir / "Shaders" / "skybox.ps.hlsl");
        if (!m_vs->IsValid() || !m_ps->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Skybox: shader 構築失敗");
            return;
        }

        m_cubeMesh->CreateInputLayout(*m_vs);

        BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(SkyboxCB));
        m_cb = Buffer::Create(cbDesc);
        if (!m_cb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "Skybox: ConstantBuffer 構築失敗");
            return;
        }

        // スカイボックス用の描画ステートを設定する（内側を描画し、深度の更新を行わない）
        PipelineDesc pipeDesc{};
        pipeDesc.cull = CullMode::Front;
        pipeDesc.depth = DepthMode::ReadOnly;
        m_pipeline = Pipeline::Create(pipeDesc);
        if (!m_pipeline->IsValid())
            return;
        if (!CreateSkyboxSampler(device, m_sampler))
            return;

        // 未ロード時でも安全に描画できるよう、初期状態として代替画像を設定しておく
        if (!CreateMagentaCubemapFallback(device, m_cubemapSrv))
            return;

        m_usingFallback = true;
        m_valid = true;
    }

    Skybox::~Skybox() = default;

    bool Skybox::LoadCubemap(const std::filesystem::path& path)
    {
        auto* device = Gpu().device;
        if (device == nullptr)
            return false;

        ComPtr<ID3D11ShaderResourceView> newSrv;
        bool loaded = false;

        if (IsDdsExtension(path))
        {
            loaded = LoadDdsCubemap(device, path, newSrv);
        }
        else
        {
            // ディレクトリが指定された場合は、6方向の画像ファイルとして読み込みを試みる
            if (::NS::Core::FileSystem::IsDirectory(path))
            {
                auto* context = Gpu().context;
                loaded = (context != nullptr) && LoadSixFacePngCubemap(device, context, path, newSrv);
            }
            else
            {
                NS_LOG_ERROR(Graphics, "Skybox LoadCubemap: ディレクトリでも .dds でもないパス: {}", path.string());
                loaded = false;
            }
        }

        if (loaded && newSrv)
        {
            m_cubemapSrv = std::move(newSrv);
            m_usingFallback = false;
            return true;
        }

        m_usingFallback = true;
        return false;
    }

    void IssueSkybox(Renderer& renderer, const Skybox& skybox, const NS::Core::Matrix& viewProjNoTranslate) noexcept
    {
        if (!skybox.IsValid())
            return;

        auto& cmd = renderer.Commands();
        if (cmd.Native() == nullptr)
            return;

        SkyboxCB cbData{};
        cbData.viewProj = viewProjNoTranslate;
        cmd.UpdateSubresource(*skybox.ConstantBuffer(), &cbData, sizeof(cbData));

        // 他の描画処理に影響を与えないよう、現在のステートを退避する
        ComPtr<ID3D11DepthStencilState> prevDss;
        UINT prevStencilRef = 0;
        cmd->OMGetDepthStencilState(prevDss.GetAddressOf(), &prevStencilRef);

        ComPtr<ID3D11RasterizerState> prevRs;
        cmd->RSGetState(prevRs.GetAddressOf());

        ComPtr<ID3D11BlendState> prevBlend;
        float prevBlendFactor[4] = {};
        UINT prevSampleMask = 0xFFFFFFFFu;
        cmd->OMGetBlendState(prevBlend.GetAddressOf(), prevBlendFactor, &prevSampleMask);

        cmd.SetPipeline(*skybox.RenderPipeline());

        cmd.VSSetShader(*skybox.VertexShader());
        cmd.PSSetShader(*skybox.PixelShader());
        cmd.VSSetConstantBuffer(*skybox.ConstantBuffer(), 0);

        ID3D11ShaderResourceView* srvs[1] = {skybox.Srv()};
        cmd->PSSetShaderResources(0, 1, srvs);

        cmd.PSSetSampler(skybox.Sampler(), 0);

        DrawMesh(cmd, *skybox.CubeMesh());

        // 警告やバグを防ぐため、使用したテクスチャのバインドを解除する
        ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
        cmd->PSSetShaderResources(0, 1, nullSrv);

        cmd->OMSetDepthStencilState(prevDss.Get(), prevStencilRef);
        cmd->RSSetState(prevRs.Get());
        cmd->OMSetBlendState(prevBlend.Get(), prevBlendFactor, prevSampleMask);
    }

    bool Skybox::IsValid() const noexcept
    {
        return m_valid;
    }

    bool Skybox::IsUsingFallback() const noexcept
    {
        return m_usingFallback;
    }

    ID3D11ShaderResourceView* Skybox::Srv() const noexcept
    {
        return m_cubemapSrv.Get();
    }

    const Pipeline* Skybox::RenderPipeline() const noexcept
    {
        return m_pipeline.get();
    }

    const Buffer* Skybox::ConstantBuffer() const noexcept
    {
        return m_cb.get();
    }

    const Shader* Skybox::VertexShader() const noexcept
    {
        return m_vs.get();
    }

    const Shader* Skybox::PixelShader() const noexcept
    {
        return m_ps.get();
    }

    ID3D11SamplerState* Skybox::Sampler() const noexcept
    {
        return m_sampler.Get();
    }

    const Mesh* Skybox::CubeMesh() const noexcept
    {
        return m_cubeMesh.get();
    }

} // namespace NS::Graphics
