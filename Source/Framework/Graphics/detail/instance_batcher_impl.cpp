#include "Framework/Graphics/InstanceBatcher.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    using detail::ComPtr;

    namespace
    {
        // 1 bucket あたりの instance 上限。 1000 instance / 48 bucket の実線は 20~21 だが、
        // 5x safety で 5000 まで確保しておく。 超えた場合は描画 skip + ERROR ログ
        constexpr std::size_t kInitialPerBucketCapacity = 5000;

        struct BucketKey
        {
            StaticMesh* mesh = nullptr;
            Material* material = nullptr;

            [[nodiscard]] bool operator==(const BucketKey& rhs) const noexcept
            {
                return mesh == rhs.mesh && material == rhs.material;
            }
        };

        struct BucketKeyHash
        {
            [[nodiscard]] std::size_t operator()(const BucketKey& k) const noexcept
            {
                const auto a = reinterpret_cast<std::uintptr_t>(k.mesh);
                const auto b = reinterpret_cast<std::uintptr_t>(k.material);
                // 64bit 領域に address 2 つを xor で混ぜる素朴な hash。 衝突は事実上ゼロ
                return static_cast<std::size_t>(a ^ ((b << 32) | (b >> 32)));
            }
        };

        struct Bucket
        {
            std::vector<BlockInstance> instances{};
        };

        // .hlsl をオンメモリ compile して bytecode blob を返す。 entryPoint / target は VS / PS 両用
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
                const char* err = errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : "<no err blob>";
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

        // POSITION + TEXCOORD + NORMAL (slot0) + INSTANCE_WORLD0..3 + INSTANCE_COLOR (slot1) の
        // 8 element input layout を組む。 Shader の InputElement 抽象は slot 切替や
        // per-instance step rate を露出していないため、 この impl 内に閉じた生 D3D11 layout で構築する
        [[nodiscard]] ComPtr<ID3D11InputLayout> CreateInstancedInputLayout(ID3D11Device* device,
                                                                           const void* vsBytecode,
                                                                           std::size_t vsBytecodeSize) noexcept
        {
            // slot 1 の AlignedByteOffset (0/16/32/48/64) は BlockInstance struct (sizeof==80) と整合
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
    } // namespace

    struct InstanceBatcher::Impl
    {
        std::unordered_map<BucketKey, Bucket, BucketKeyHash> buckets{};

        ComPtr<ID3D11Device> device;

        // 動的 instance VB + 自前 compile した VS / PS / input layout。 production 描画パスのみで使う
        std::unique_ptr<VertexBuffer> instanceVB;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11InputLayout> inputLayout;
        std::size_t instanceVbCapacity = 0;

        std::size_t lastDrawCallCount = 0;
        bool valid = false;
        bool countOnlyMode = false;
    };

    namespace
    {
        [[nodiscard]] std::unique_ptr<VertexBuffer> CreateInstanceVB(Renderer& renderer, std::size_t capacity) noexcept
        {
            VertexBufferDesc desc{};
            desc.initialData = nullptr; // Dynamic は initial data 不要 (Map で都度書込)
            desc.vertexCount = capacity;
            desc.stride = sizeof(BlockInstance);
            desc.usage = BufferUsage::Dynamic;
            auto vb = std::make_unique<VertexBuffer>(renderer, desc);
            if (!vb->IsValid())
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "InstanceBatcher: instance VB 構築失敗 (capacity={})", capacity);
                return {};
            }
            return vb;
        }
    } // namespace

    InstanceBatcher::InstanceBatcher(Renderer& renderer) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        auto* context = detail::GetContext(renderer);
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "InstanceBatcher: Renderer の Device / Context が無効");
            return;
        }
        m_pImpl->device = device;

        m_pImpl->instanceVB = CreateInstanceVB(renderer, kInitialPerBucketCapacity);
        if (!m_pImpl->instanceVB)
        {
            return;
        }
        m_pImpl->instanceVbCapacity = kInitialPerBucketCapacity;

        // Shader の InputElement は slot 0 単 stream 専用なので、 本 batcher は直接 D3D11 で VS+PS を組む
        // PS は standard.ps.hlsl を流用する (worldNormal / uv 出力を期待する PS と整合)
        // VS_OUT が standard と完全一致しない点 (instColor の有無等) は将来の lighting 拡張で reconcile
        const auto exeDir = ::NS::Core::FileSystem::GetExeDirectory();
        const auto vsPath = exeDir / "Shaders" / "instanced.vs.hlsl";
        const auto psPath = exeDir / "Shaders" / "standard.ps.hlsl";

        ComPtr<ID3DBlob> vsBlob;
        if (!CompileHlsl(vsPath, "VSMain", "vs_5_0", vsBlob))
        {
            return;
        }
        ComPtr<ID3DBlob> psBlob;
        if (!CompileHlsl(psPath, "PSMain", "ps_5_0", psBlob))
        {
            return;
        }

        HRESULT hr = device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_pImpl->vs.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "InstanceBatcher: CreateVertexShader 失敗 (hr=0x{:08X})",
                         static_cast<unsigned>(hr));
            return;
        }
        hr = device->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pImpl->ps.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "InstanceBatcher: CreatePixelShader 失敗 (hr=0x{:08X})",
                         static_cast<unsigned>(hr));
            return;
        }

        m_pImpl->inputLayout = CreateInstancedInputLayout(device, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());
        if (!m_pImpl->inputLayout)
        {
            return;
        }

        m_pImpl->valid = true;
    }

    InstanceBatcher::~InstanceBatcher() = default;

    void InstanceBatcher::BeginFrame() noexcept
    {
        if (!m_pImpl)
            return;
        for (auto& [key, bucket] : m_pImpl->buckets)
        {
            bucket.instances.clear();
        }
        m_pImpl->lastDrawCallCount = 0;
    }

    void InstanceBatcher::Submit(StaticMesh* mesh, Material* material, const BlockInstance& instance)
    {
        if (!m_pImpl)
            return;
        if (mesh == nullptr || material == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "InstanceBatcher::Submit: mesh / material は非 null 必須 (mesh={}, material={})",
                         static_cast<void*>(mesh),
                         static_cast<void*>(material));
            return;
        }
        const BucketKey key{mesh, material};
        m_pImpl->buckets[key].instances.push_back(instance);
    }

    void InstanceBatcher::FlushAll(Renderer& renderer) noexcept
    {
        if (!m_pImpl)
            return;

        std::size_t drawCalls = 0;
        const bool canDraw = m_pImpl->valid && !m_pImpl->countOnlyMode;
        auto* ctx = detail::GetContext(renderer);

        for (auto& [key, bucket] : m_pImpl->buckets)
        {
            if (bucket.instances.empty())
                continue;

            if (canDraw && ctx != nullptr)
            {
                if (bucket.instances.size() > m_pImpl->instanceVbCapacity)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "InstanceBatcher: bucket 容量 {} は VB capacity {} を超過、 描画スキップ",
                                 bucket.instances.size(),
                                 m_pImpl->instanceVbCapacity);
                    continue;
                }

                const std::size_t bytes = bucket.instances.size() * sizeof(BlockInstance);
                renderer.UpdateBuffer(*m_pImpl->instanceVB, bucket.instances.data(), bytes);

                // material が握っている texture / sampler / CB を bind、 ただし VS / PS / InputLayout は
                // 直後に instance 用で上書きする。 material 側 shader は使わない
                key.material->Bind(renderer);

                ctx->VSSetShader(m_pImpl->vs.Get(), nullptr, 0);
                ctx->PSSetShader(m_pImpl->ps.Get(), nullptr, 0);
                ctx->IASetInputLayout(m_pImpl->inputLayout.Get());

                ID3D11Buffer* meshVB = detail::GetVertexBuffer(*key.mesh);
                ID3D11Buffer* meshIB = detail::GetIndexBuffer(*key.mesh);
                ID3D11Buffer* instanceVB = detail::GetNative(*m_pImpl->instanceVB);
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
                ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
                ctx->IASetIndexBuffer(meshIB, DXGI_FORMAT_R32_UINT, 0);
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                ctx->DrawIndexedInstanced(
                    static_cast<UINT>(key.mesh->IndexCount()), static_cast<UINT>(bucket.instances.size()), 0, 0, 0);
            }
            ++drawCalls;
        }
        m_pImpl->lastDrawCallCount = drawCalls;
    }

    std::size_t InstanceBatcher::BucketCount() const noexcept
    {
        if (!m_pImpl)
            return 0;
        std::size_t nonEmpty = 0;
        for (const auto& [key, bucket] : m_pImpl->buckets)
        {
            if (!bucket.instances.empty())
                ++nonEmpty;
        }
        return nonEmpty;
    }

    std::size_t InstanceBatcher::LastFrameDrawCallCount() const noexcept
    {
        return m_pImpl ? m_pImpl->lastDrawCallCount : 0;
    }

    bool InstanceBatcher::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }

    namespace detail
    {
        void SetCountOnlyMode(InstanceBatcher& batcher, bool countOnly) noexcept
        {
            if (batcher.m_pImpl)
            {
                batcher.m_pImpl->countOnlyMode = countOnly;
            }
        }
    } // namespace detail

} // namespace NS::Graphics
