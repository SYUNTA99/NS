#include "Framework/Graphics/InstanceBatcher.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/StaticMesh.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <d3dcompiler.h>

#include <algorithm>

namespace NS::Graphics
{

    namespace
    {
        // 1 bucket あたりの instance 上限。 1000 instance / 48 bucket の実線は 20~21 だが、
        // 5x safety で 5000 まで確保しておく。 超えた場合は描画 skip + ERROR ログ
        constexpr std::size_t kInitialPerBucketCapacity = 5000;

        // entryPoint / target を差し替えて VS / PS 両方のコンパイルに使う
        [[nodiscard]] bool CompileHlsl(const std::filesystem::path& path,
                                       const char* entryPoint,
                                       const char* target,
                                       ComPtr<ID3DBlob>& outBlob) noexcept
        {
            auto bytesOpt = ::NS::Core::FileSystem::ReadAllBytes(path);
            if (!bytesOpt.has_value())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "InstanceBatcher: hlsl 読込失敗 ({})", path.string());
                return false;
            }
            const auto& bytes = bytesOpt.value();

            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

            const std::string sourceName = path.string();
            ComPtr<ID3DBlob> errBlob;
            const HRESULT hr = D3DCompile(bytes.data(),
                                          bytes.size(),
                                          sourceName.c_str(),
                                          nullptr,
                                          D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                          entryPoint,
                                          target,
                                          flags,
                                          0,
                                          outBlob.GetAddressOf(),
                                          errBlob.GetAddressOf());
            if (FAILED(hr))
            {
                const char* err = [&]() -> const char* {
                    if (errBlob)
                    {
                        return static_cast<const char*>(errBlob->GetBufferPointer());
                    }
                    return "<no err blob>";
                }();
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "InstanceBatcher: hlsl コンパイル失敗 ({} / {}, hr=0x{:08X}): {}",
                             sourceName,
                             entryPoint,
                             static_cast<unsigned>(hr),
                             err);
                return false;
            }
            return true;
        }

        // slot0 の POSITION+TEXCOORD+NORMAL と slot1 の INSTANCE_WORLD0..3+INSTANCE_COLOR で 8 要素 layout。slot
        // 切替・per-instance step rate は Shader 抽象に非公開のため生 D3D11 で構築
        [[nodiscard]] ComPtr<ID3D11InputLayout> CreateInstancedInputLayout(ID3D11Device* device,
                                                                           const void* vsBytecode,
                                                                           std::size_t vsBytecodeSize) noexcept
        {
            // slot 1 の AlignedByteOffset 0/16/32/48/64 は sizeof==80 の BlockInstance struct と整合
            // BlockInstance 側 static_assert で stride 不一致を防いでいる
            const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            };
            constexpr UINT layoutCount = sizeof(layoutDesc) / sizeof(layoutDesc[0]);

            ComPtr<ID3D11InputLayout> layout;
            const HRESULT hr =
                device->CreateInputLayout(layoutDesc, layoutCount, vsBytecode, vsBytecodeSize, layout.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "InstanceBatcher: CreateInputLayout 失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
                return {};
            }
            return layout;
        }

        [[nodiscard]] std::unique_ptr<Buffer> CreateInstanceVB(std::size_t capacity) noexcept
        {
            // Dynamic は initial data 不要で Map で都度書込
            BufferDesc vbDesc = MakeVertexBufferDesc(nullptr, capacity, sizeof(BlockInstance), D3D11_USAGE_DYNAMIC);
            std::unique_ptr<Buffer> vb = Buffer::Create(vbDesc);
            if (!vb->IsValid())
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "InstanceBatcher: instance VB 構築失敗 (capacity={})", capacity);
                return {};
            }
            return vb;
        }
    } // namespace

    std::unique_ptr<InstanceBatcher> InstanceBatcher::Create()
    {
        return std::unique_ptr<InstanceBatcher>(new InstanceBatcher());
    }

    InstanceBatcher::InstanceBatcher()
    {
        auto* device = Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "InstanceBatcher: グローバル Device が無効");
            return;
        }
        m_device = device;

        m_instanceVB = CreateInstanceVB(kInitialPerBucketCapacity);
        if (!m_instanceVB)
        {
            return;
        }
        m_instanceVbCapacity = kInitialPerBucketCapacity;

        if (!BuildShaders())
        {
            return;
        }

        m_valid = true;
    }

    bool InstanceBatcher::BuildShaders() noexcept
    {
        if (m_device == nullptr)
        {
            return false;
        }

        // Shader は slot 0 単 stream 専用のため batcher が直接 VS+PS を組む。PS は standard.ps.hlsl 流用
        const auto exeDir = ::NS::Core::FileSystem::ContentRoot();
        const auto vsPath = exeDir / "Shaders" / "instanced.vs.hlsl";
        const auto psPath = exeDir / "Shaders" / "standard.ps.hlsl";

        ComPtr<ID3DBlob> vsBlob;
        if (!CompileHlsl(vsPath, "VSMain", "vs_5_0", vsBlob))
        {
            return false;
        }
        ComPtr<ID3DBlob> psBlob;
        if (!CompileHlsl(psPath, "PSMain", "ps_5_0", psBlob))
        {
            return false;
        }

        ComPtr<ID3D11VertexShader> vs;
        HRESULT hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "InstanceBatcher: CreateVertexShader 失敗 (hr=0x{:08X})",
                         static_cast<unsigned>(hr));
            return false;
        }
        ComPtr<ID3D11PixelShader> ps;
        hr = m_device->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "InstanceBatcher: CreatePixelShader 失敗 (hr=0x{:08X})",
                         static_cast<unsigned>(hr));
            return false;
        }
        ComPtr<ID3D11InputLayout> layout =
            CreateInstancedInputLayout(m_device.Get(), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());
        if (!layout)
        {
            return false;
        }

        // 全段成功してから差し替える。 reload 失敗時に旧 shader / layout を壊さない
        m_vs = std::move(vs);
        m_ps = std::move(ps);
        m_inputLayout = std::move(layout);
        return true;
    }

    void InstanceBatcher::ReloadShaders() noexcept
    {
        if (m_device == nullptr)
        {
            return;
        }
        if (BuildShaders())
        {
            NS_LOG_INFO(::NS::Core::LogCat::Graphics, "InstanceBatcher: instanced shader reload 成功");
        }
    }

    InstanceBatcher::~InstanceBatcher() = default;

    void InstanceBatcher::BeginFrame() noexcept
    {
        for (auto& [key, bucket] : m_buckets)
        {
            bucket.instances.clear();
        }
        m_lastDrawCallCount = 0;
    }

    void InstanceBatcher::Submit(StaticMesh* mesh, Material* material, const BlockInstance& instance)
    {
        if (mesh == nullptr || material == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "InstanceBatcher::Submit: mesh / material は非 null 必須 (mesh={}, material={})",
                         static_cast<void*>(mesh),
                         static_cast<void*>(material));
            return;
        }
        const BucketKey key{mesh, material};
        m_buckets[key].instances.push_back(instance);
    }

    void InstanceBatcher::FlushAll(Renderer& renderer) noexcept
    {
        std::size_t drawCalls = 0;
        const bool canDraw = m_valid && !m_countOnlyMode;
        auto* context = Gpu().context;

        // block は不透明固定、前の描画者が残した state を断つため毎回 Pipeline を set し直す
        if (canDraw && context != nullptr)
            renderer.Commands().SetPipeline(renderer.CommonPipeline(BlendMode::Opaque));

        for (auto& [key, bucket] : m_buckets)
        {
            if (bucket.instances.empty())
                continue;

            if (canDraw && context != nullptr)
            {
                if (bucket.instances.size() > m_instanceVbCapacity)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "InstanceBatcher: bucket 容量 {} は VB capacity {} を超過、 描画スキップ",
                                 bucket.instances.size(),
                                 m_instanceVbCapacity);
                    continue;
                }

                const std::size_t bytes = bucket.instances.size() * sizeof(BlockInstance);
                renderer.Commands().UpdateBuffer(*m_instanceVB, bucket.instances.data(), bytes);

                // material が握っている texture / sampler / CB を bind、 ただし VS / PS / InputLayout は
                // 直後に instance 用で上書きする。 material 側 shader は使わない
                key.material->Bind(renderer);

                context->VSSetShader(m_vs.Get(), nullptr, 0);
                context->PSSetShader(m_ps.Get(), nullptr, 0);
                context->IASetInputLayout(m_inputLayout.Get());

                const Buffer* meshVBuf = key.mesh->VertexBuffer();
                const Buffer* meshIBuf = key.mesh->IndexBuffer();
                ID3D11Buffer* meshVB = nullptr;
                if (meshVBuf)
                {
                    meshVB = meshVBuf->Native();
                }
                ID3D11Buffer* meshIB = nullptr;
                if (meshIBuf)
                {
                    meshIB = meshIBuf->Native();
                }
                ID3D11Buffer* instanceVB = m_instanceVB->Native();
                if (meshVB == nullptr || meshIB == nullptr || instanceVB == nullptr)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "InstanceBatcher: bucket の mesh VB / IB / instance VB が null、 描画スキップ");
                    continue;
                }

                ID3D11Buffer* vbs[2] = {meshVB, instanceVB};
                const UINT strides[2] = {static_cast<UINT>(sizeof(StaticVertex)),
                                         static_cast<UINT>(sizeof(BlockInstance))};
                const UINT offsets[2] = {0, 0};
                context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
                context->IASetIndexBuffer(meshIB, meshIBuf->Format(), 0);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                context->DrawIndexedInstanced(
                    static_cast<UINT>(key.mesh->IndexCount()), static_cast<UINT>(bucket.instances.size()), 0, 0, 0);
            }
            ++drawCalls;
        }
        m_lastDrawCallCount = drawCalls;
    }

    std::size_t InstanceBatcher::BucketCount() const noexcept
    {
        std::size_t nonEmpty = 0;
        for (const auto& [key, bucket] : m_buckets)
        {
            if (!bucket.instances.empty())
                ++nonEmpty;
        }
        return nonEmpty;
    }

    std::size_t InstanceBatcher::LastFrameDrawCallCount() const noexcept
    {
        return m_lastDrawCallCount;
    }

    bool InstanceBatcher::IsValid() const noexcept
    {
        return m_valid;
    }

    void InstanceBatcher::SetCountOnlyMode(bool countOnly) noexcept
    {
        m_countOnlyMode = countOnly;
    }

} // namespace NS::Graphics
