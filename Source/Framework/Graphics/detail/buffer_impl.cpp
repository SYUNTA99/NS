#include "Framework/Graphics/Buffer.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cstring>

namespace NS::Graphics
{
    using detail::ComPtr;

    namespace
    {
        constexpr std::size_t kConstantBufferAlignment = 16;

        [[nodiscard]] DXGI_FORMAT ToDxgiFormat(IndexFormat fmt) noexcept
        {
            switch (fmt)
            {
            case IndexFormat::UInt16:
                return DXGI_FORMAT_R16_UINT;
            case IndexFormat::UInt32:
            default:
                return DXGI_FORMAT_R32_UINT;
            }
        }

        [[nodiscard]] std::size_t RoundUpToAlignment(std::size_t bytes, std::size_t alignment) noexcept
        {
            return (bytes + alignment - 1u) & ~(alignment - 1u);
        }

        bool CreateD3DBuffer(ID3D11Device* device,
                             std::size_t byteSize,
                             UINT bindFlag,
                             BufferUsage usage,
                             const void* initialData,
                             ComPtr<ID3D11Buffer>& outBuffer) noexcept
        {
            if (device == nullptr || byteSize == 0u)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CreateD3DBuffer: 引数不正 (device={}, byteSize={})",
                             static_cast<const void*>(device),
                             byteSize);
                return false;
            }
            if (usage == BufferUsage::Static && initialData == nullptr)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Static バッファは initialData 必須 (CreateBuffer 後 GPU が未初期化を読む危険)");
                return false;
            }

            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(byteSize);
            desc.BindFlags = bindFlag;
            if (usage == BufferUsage::Dynamic)
            {
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            }
            else
            {
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.CPUAccessFlags = 0;
            }

            D3D11_SUBRESOURCE_DATA srd{};
            D3D11_SUBRESOURCE_DATA* srdPtr = nullptr;
            if (initialData != nullptr)
            {
                srd.pSysMem = initialData;
                srdPtr = &srd;
            }

            const HRESULT hr = device->CreateBuffer(&desc, srdPtr, outBuffer.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CreateBuffer 失敗 (hr=0x{:X}, bytes={}, bind=0x{:X})",
                             static_cast<unsigned>(hr),
                             byteSize,
                             static_cast<unsigned>(bindFlag));
                return false;
            }
            return true;
        }

        bool MapAndCopy(ID3D11DeviceContext* context,
                        ID3D11Buffer* buffer,
                        const void* data,
                        std::size_t bytes) noexcept
        {
            if (context == nullptr || buffer == nullptr || data == nullptr || bytes == 0u)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Buffer::MapAndCopy: 引数不正 (context={}, buffer={}, data={}, bytes={})",
                             static_cast<const void*>(context),
                             static_cast<const void*>(buffer),
                             data,
                             bytes);
                return false;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Buffer::Map 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            std::memcpy(mapped.pData, data, bytes);
            context->Unmap(buffer, 0);
            return true;
        }
    } // namespace

    struct VertexBuffer::Impl
    {
        ComPtr<ID3D11Buffer> buffer;
        std::size_t stride = 0;
        std::size_t vertexCount = 0;
        BufferUsage usage = BufferUsage::Static;
        bool valid = false;
    };

    VertexBuffer::VertexBuffer(Renderer& renderer, const VertexBufferDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        m_pImpl->stride = desc.stride;
        m_pImpl->vertexCount = desc.vertexCount;
        m_pImpl->usage = desc.usage;

        auto* device = detail::GetDevice(renderer);
        if (device == nullptr || desc.stride == 0u || desc.vertexCount == 0u)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "VertexBuffer 構築失敗 (device={}, stride={}, count={})",
                         static_cast<const void*>(device),
                         desc.stride,
                         desc.vertexCount);
            return;
        }

        const std::size_t bytes = desc.stride * desc.vertexCount;
        if (!CreateD3DBuffer(device, bytes, D3D11_BIND_VERTEX_BUFFER, desc.usage, desc.initialData, m_pImpl->buffer))
        {
            return;
        }
        m_pImpl->valid = true;
    }

    VertexBuffer::~VertexBuffer() = default;

    bool VertexBuffer::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }
    std::size_t VertexBuffer::Stride() const noexcept
    {
        return m_pImpl ? m_pImpl->stride : 0u;
    }
    std::size_t VertexBuffer::VertexCount() const noexcept
    {
        return m_pImpl ? m_pImpl->vertexCount : 0u;
    }

    struct IndexBuffer::Impl
    {
        ComPtr<ID3D11Buffer> buffer;
        IndexFormat format = IndexFormat::UInt32;
        std::size_t indexCount = 0;
        BufferUsage usage = BufferUsage::Static;
        bool valid = false;
    };

    IndexBuffer::IndexBuffer(Renderer& renderer, const IndexBufferDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        m_pImpl->format = desc.format;
        m_pImpl->indexCount = desc.indexCount;
        m_pImpl->usage = desc.usage;

        auto* device = detail::GetDevice(renderer);
        if (device == nullptr || desc.indexCount == 0u)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "IndexBuffer 構築失敗 (device={}, count={})",
                         static_cast<const void*>(device),
                         desc.indexCount);
            return;
        }

        const std::size_t elementSize = (desc.format == IndexFormat::UInt16) ? 2u : 4u;
        const std::size_t bytes = elementSize * desc.indexCount;
        if (!CreateD3DBuffer(device, bytes, D3D11_BIND_INDEX_BUFFER, desc.usage, desc.initialData, m_pImpl->buffer))
        {
            return;
        }
        m_pImpl->valid = true;
    }

    IndexBuffer::~IndexBuffer() = default;

    bool IndexBuffer::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }
    IndexFormat IndexBuffer::Format() const noexcept
    {
        return m_pImpl ? m_pImpl->format : IndexFormat::UInt32;
    }
    std::size_t IndexBuffer::IndexCount() const noexcept
    {
        return m_pImpl ? m_pImpl->indexCount : 0u;
    }

    struct ConstantBuffer::Impl
    {
        ComPtr<ID3D11Buffer> buffer;
        std::size_t byteSize = 0;
        bool valid = false;
    };

    ConstantBuffer::ConstantBuffer(Renderer& renderer, std::size_t byteSize) : m_pImpl(std::make_unique<Impl>())
    {
        const std::size_t rounded = RoundUpToAlignment(byteSize, kConstantBufferAlignment);
        m_pImpl->byteSize = rounded;

        auto* device = detail::GetDevice(renderer);
        if (device == nullptr || rounded == 0u)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "ConstantBuffer 構築失敗 (device={}, bytes={})",
                         static_cast<const void*>(device),
                         rounded);
            return;
        }

        if (!CreateD3DBuffer(
                device, rounded, D3D11_BIND_CONSTANT_BUFFER, BufferUsage::Dynamic, nullptr, m_pImpl->buffer))
        {
            return;
        }
        m_pImpl->valid = true;
    }

    ConstantBuffer::~ConstantBuffer() = default;

    bool ConstantBuffer::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }
    std::size_t ConstantBuffer::ByteSize() const noexcept
    {
        return m_pImpl ? m_pImpl->byteSize : 0u;
    }

    namespace detail
    {
        ID3D11Buffer* GetNative(VertexBuffer& vb) noexcept
        {
            return (vb.m_pImpl) ? vb.m_pImpl->buffer.Get() : nullptr;
        }

        ID3D11Buffer* GetNative(IndexBuffer& ib) noexcept
        {
            return (ib.m_pImpl) ? ib.m_pImpl->buffer.Get() : nullptr;
        }

        ID3D11Buffer* GetNative(ConstantBuffer& cb) noexcept
        {
            return (cb.m_pImpl) ? cb.m_pImpl->buffer.Get() : nullptr;
        }

        void BindVertexBuffer(ID3D11DeviceContext* context, VertexBuffer& vb, unsigned slot) noexcept
        {
            if (context == nullptr || !vb.m_pImpl || !vb.m_pImpl->valid)
            {
                return;
            }
            ID3D11Buffer* buffers[1] = {vb.m_pImpl->buffer.Get()};
            const UINT stride = static_cast<UINT>(vb.m_pImpl->stride);
            const UINT offset = 0u;
            context->IASetVertexBuffers(slot, 1u, buffers, &stride, &offset);
        }

        void UpdateVertexBufferRaw(ID3D11DeviceContext* context,
                                   VertexBuffer& vb,
                                   const void* data,
                                   std::size_t bytes) noexcept
        {
            if (context == nullptr || !vb.m_pImpl || !vb.m_pImpl->valid)
            {
                return;
            }
            if (vb.m_pImpl->usage != BufferUsage::Dynamic)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Static VertexBuffer に対する Update は無効 (Dynamic で再構築するか別バッファを使う)");
                return;
            }
            const std::size_t maxBytes = vb.m_pImpl->stride * vb.m_pImpl->vertexCount;
            if (bytes > maxBytes)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "VertexBuffer::Update のサイズ超過 (req={}, max={})",
                             bytes,
                             maxBytes);
                return;
            }
            MapAndCopy(context, vb.m_pImpl->buffer.Get(), data, bytes);
        }

        void BindIndexBuffer(ID3D11DeviceContext* context, IndexBuffer& ib) noexcept
        {
            if (context == nullptr || !ib.m_pImpl || !ib.m_pImpl->valid)
            {
                return;
            }
            context->IASetIndexBuffer(ib.m_pImpl->buffer.Get(), ToDxgiFormat(ib.m_pImpl->format), 0u);
        }

        void BindConstantBuffer(ID3D11DeviceContext* context,
                                ConstantBuffer& cb,
                                unsigned slot,
                                ShaderStage stages) noexcept
        {
            if (context == nullptr || !cb.m_pImpl || !cb.m_pImpl->valid)
            {
                return;
            }
            ID3D11Buffer* buffers[1] = {cb.m_pImpl->buffer.Get()};
            if (HasStage(stages, ShaderStage::Vertex))
            {
                context->VSSetConstantBuffers(slot, 1u, buffers);
            }
            if (HasStage(stages, ShaderStage::Pixel))
            {
                context->PSSetConstantBuffers(slot, 1u, buffers);
            }
            if (HasStage(stages, ShaderStage::Geometry))
            {
                context->GSSetConstantBuffers(slot, 1u, buffers);
            }
        }

        void UpdateConstantBufferRaw(ID3D11DeviceContext* context,
                                     ConstantBuffer& cb,
                                     const void* data,
                                     std::size_t bytes) noexcept
        {
            if (!cb.m_pImpl || !cb.m_pImpl->valid)
            {
                return;
            }
            if (bytes > cb.m_pImpl->byteSize)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "ConstantBuffer::Update のサイズ超過 (req={}, max={})",
                             bytes,
                             cb.m_pImpl->byteSize);
                return;
            }
            if ((bytes % kConstantBufferAlignment) != 0u)
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "ConstantBuffer::Update のサイズは 16-byte 倍数必須 (req={})", bytes);
                return;
            }
            MapAndCopy(context, cb.m_pImpl->buffer.Get(), data, bytes);
        }
    } // namespace detail

} // namespace NS::Graphics
