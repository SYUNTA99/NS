#include "Framework/Graphics/Skybox.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/StaticMesh.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace NS::Graphics
{

    namespace
    {
        // Skybox VS の SkyboxCB と一致するレイアウト。 row_major float4x4 のみ 64 byte
        struct alignas(16) SkyboxCB
        {
            NS::Math::Matrix viewProj;
        };
        static_assert(sizeof(SkyboxCB) == 64, "SkyboxCB は HLSL 側 cbuffer (b0) と byte 一致が必要");

        // kurt placeholder の 6 face レイアウト。 D3D11 cubemap の標準順は +X / -X / +Y / -Y / +Z / -Z
        // kurt の命名 (rt / lf / up / dn / ft / bk) は左手系 LH カメラから見た方向にマップする
        // 視覚的に上下逆や水平反転がある場合は個別差替え
        constexpr std::array<const char*, 6> kKurtFaceFileNames = {
            "space_rt.png", // +X (right)
            "space_lf.png", // -X (left)
            "space_up.png", // +Y (top)
            "space_dn.png", // -Y (bottom)
            "space_ft.png", // +Z (forward, LH)
            "space_bk.png", // -Z (back,    LH)
        };

        [[nodiscard]] bool IsDdsExtension(const std::filesystem::path& path)
        {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext == ".dds";
        }

        // 1x1 マゼンタ cubemap fallback。 ロード未呼出 / 失敗時に使用、 Render の安全保証
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox fallback cubemap CreateTexture2D 失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox fallback cubemap CreateSRV 失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox .dds ロード失敗: {} (hr=0x{:08X})",
                             path.string(),
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        // 6 枚 PNG を読み込み、 cubemap として束ねた Texture2D + SRV を作る
        // 各 face は 2D Texture2D として WIC で staging に読み込み、 そこから
        // ArraySize=6 + MISC_TEXTURECUBE の本体テクスチャに CopySubresourceRegion で転写する
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
                const std::filesystem::path facePath = dir / kKurtFaceFileNames[i];
                if (!::NS::Core::FileSystem::Exists(facePath))
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Skybox 6-face: {} が見つからない", facePath.string());
                    return false;
                }

                auto bytesOpt = ::NS::Core::FileSystem::ReadAllBytes(facePath);
                if (!bytesOpt.has_value())
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Skybox 6-face: {} 読込失敗", facePath.string());
                    return false;
                }
                const auto& bytes = bytesOpt.value();

                ComPtr<ID3D11Resource> resource;
                ComPtr<ID3D11ShaderResourceView> tmpSrv;
                // staging 用に CPU からアクセス可能な形式で読込み、 後で CopySubresourceRegion でコピーする
                // WIC はデフォルトで RGBA8 に正規化される
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
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "Skybox 6-face: WIC 読込失敗 {} (hr=0x{:08X})",
                                 facePath.string(),
                                 static_cast<unsigned>(hr));
                    return false;
                }

                ComPtr<ID3D11Texture2D> tex2d;
                if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex2d.GetAddressOf()))) || !tex2d)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "Skybox 6-face: ID3D11Texture2D へ QI 失敗 {}",
                                 facePath.string());
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
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "Skybox 6-face: face {} の解像度/フォーマット不一致 ({}x{}, fmt={})",
                                 facePath.string(),
                                 static_cast<int>(d.Width),
                                 static_cast<int>(d.Height),
                                 static_cast<int>(d.Format));
                    return false;
                }
                faceTextures[i] = std::move(tex2d);
            }

            // 6-face cubemap 本体を作る。 mipmap は v1 では生成しない (置物 placeholder、
            // theme 確定後に texconv で .dds 直接配布に切り替える運用)
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox 6-face: cubemap SRV 作成失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        bool CreateSkyboxDepthState(ID3D11Device* device,
                                    D3D11_DEPTH_STENCIL_DESC& outDesc,
                                    ComPtr<ID3D11DepthStencilState>& outState) noexcept
        {
            outDesc = {};
            outDesc.DepthEnable = TRUE;
            outDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            outDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            outDesc.StencilEnable = FALSE;
            outDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
            outDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

            const HRESULT hr = device->CreateDepthStencilState(&outDesc, outState.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox DepthStencilState 作成失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        bool CreateSkyboxRasterState(ID3D11Device* device,
                                     D3D11_RASTERIZER_DESC& outDesc,
                                     ComPtr<ID3D11RasterizerState>& outState) noexcept
        {
            outDesc = {};
            outDesc.FillMode = D3D11_FILL_SOLID;
            // inside-out cube なので前面を捨てる。 NS の他の不透明描画は CW = front
            // (Mesh の MakeCube が CW front)。 FrontCCW=FALSE のまま CullMode=FRONT で背面が残る
            outDesc.CullMode = D3D11_CULL_FRONT;
            outDesc.FrontCounterClockwise = FALSE;
            outDesc.DepthClipEnable = TRUE;

            const HRESULT hr = device->CreateRasterizerState(&outDesc, outState.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox RasterizerState 作成失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox SamplerState 作成失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
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
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Skybox: グローバル Device が無効");
            return;
        }
        m_device = device;

        // unit cube mesh。 inside-out 描画なのでサイズは何でも良いが、 1m 立方 (half=0.5) で統一
        auto geom = MakeCube({0.5f, 0.5f, 0.5f});
        MeshDesc md{};
        md.vertices = geom.vertices.data();
        md.vertexCount = geom.vertices.size();
        md.indices = geom.indices.data();
        md.indexCount = geom.indices.size();
        m_cubeMesh = StaticMesh::Create(md);
        if (!m_cubeMesh->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Skybox: cube mesh 構築失敗");
            return;
        }

        // skybox 専用 shader。 cube mesh の StandardInputLayout (POSITION+TEXCOORD+NORMAL) と
        // skybox.vs の入力シグネチャを共有する
        const auto exeDir = ::NS::Core::FileSystem::GetExeDirectory();
        m_vs = Shader::Create(exeDir / "Shaders" / "skybox.vs.hlsl");
        m_ps = Shader::Create(exeDir / "Shaders" / "skybox.ps.hlsl");
        if (!m_vs->IsValid() || !m_ps->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Skybox: shader 構築失敗");
            return;
        }

        // skybox は MeshRendererComponent を経由せず直接 Draw するため、 ここで InputLayout を生成する
        m_cubeMesh->CreateInputLayout(*m_vs);

        // viewProj を渡す 64 byte の CB
        BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(SkyboxCB));
        m_cb = Buffer::Create(cbDesc);
        if (!m_cb->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Skybox: ConstantBuffer 構築失敗");
            return;
        }

        if (!CreateSkyboxDepthState(device, m_depthDesc, m_depthState))
            return;
        if (!CreateSkyboxRasterState(device, m_rasterDesc, m_rasterState))
            return;
        if (!CreateSkyboxSampler(device, m_sampler))
            return;

        // LoadCubemap 未呼出でも Render が安全に動くよう fallback を必ず先に用意する
        if (!CreateMagentaCubemapFallback(device, m_cubemapSrv))
            return;

        m_usingFallback = true;
        m_valid = true;
    }

    Skybox::~Skybox() = default;

    bool Skybox::LoadCubemap(const std::filesystem::path& path)
    {
        if (!m_device)
            return false;

        ComPtr<ID3D11ShaderResourceView> newSrv;
        bool loaded = false;

        if (IsDdsExtension(path))
        {
            loaded = LoadDdsCubemap(m_device.Get(), path, newSrv);
        }
        else
        {
            // ディレクトリとして 6 face PNG を試す。 ファイルパスが渡された場合は不存在として fallback へ
            std::error_code ec;
            if (std::filesystem::is_directory(path, ec))
            {
                auto* context = Gpu().context;
                loaded = (context != nullptr) && LoadSixFacePngCubemap(m_device.Get(), context, path, newSrv);
            }
            else
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Skybox LoadCubemap: ディレクトリでも .dds でもないパス: {}",
                             path.string());
                loaded = false;
            }
        }

        if (loaded && newSrv)
        {
            m_cubemapSrv = std::move(newSrv);
            m_usingFallback = false;
            return true;
        }

        // 既存 fallback SRV をそのまま維持し、 呼出側に false を返す
        m_usingFallback = true;
        return false;
    }

    void Skybox::Render(Renderer& renderer, const NS::Math::Matrix& viewProjNoTranslate) noexcept
    {
        if (!m_valid)
            return;
        auto& cmd = renderer.Commands();
        if (cmd.Native() == nullptr)
            return;

        SkyboxCB cbData{};
        cbData.viewProj = viewProjNoTranslate;
        renderer.Commands().UpdateBuffer(*m_cb, &cbData, sizeof(cbData));

        // 既存 depth/raster state を退避して draw 後に復元する
        ComPtr<ID3D11DepthStencilState> prevDss;
        UINT prevStencilRef = 0;
        cmd->OMGetDepthStencilState(prevDss.GetAddressOf(), &prevStencilRef);

        ComPtr<ID3D11RasterizerState> prevRs;
        cmd->RSGetState(prevRs.GetAddressOf());

        cmd->OMSetDepthStencilState(m_depthState.Get(), 0);
        cmd->RSSetState(m_rasterState.Get());

        renderer.Commands().SetShader(*m_vs);
        renderer.Commands().SetShader(*m_ps);
        renderer.Commands().SetConstantBuffer(*m_cb, 0, ShaderType::Vertex);

        ID3D11ShaderResourceView* srvs[1] = {m_cubemapSrv.Get()};
        cmd->PSSetShaderResources(0, 1, srvs);

        cmd.SetSampler(m_sampler.Get(), 0, ShaderType::Pixel);

        // Mesh::Draw は VB / IB / topology / DrawIndexed を一括実行する
        m_cubeMesh->Draw(renderer);

        // バインドした SRV を解除しないと、 後段の通常 Material::Bind が同じ t0 に
        // Texture2D を再バインドする際に D3D11 ランタイムが警告を出すことがある
        ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
        cmd->PSSetShaderResources(0, 1, nullSrv);

        cmd->OMSetDepthStencilState(prevDss.Get(), prevStencilRef);
        cmd->RSSetState(prevRs.Get());
    }

    bool Skybox::IsValid() const noexcept
    {
        return m_valid;
    }

    bool Skybox::IsUsingFallback() const noexcept
    {
        return m_usingFallback;
    }

    namespace detail
    {
        ID3D11ShaderResourceView* GetCubemapSrv(Skybox& skybox) noexcept
        {
            return skybox.m_cubemapSrv.Get();
        }

        void GetDepthStateDesc(Skybox& skybox, D3D11_DEPTH_STENCIL_DESC& out) noexcept
        {
            out = skybox.m_depthDesc;
        }

        void GetRasterStateDesc(Skybox& skybox, D3D11_RASTERIZER_DESC& out) noexcept
        {
            out = skybox.m_rasterDesc;
        }
    } // namespace detail

} // namespace NS::Graphics
