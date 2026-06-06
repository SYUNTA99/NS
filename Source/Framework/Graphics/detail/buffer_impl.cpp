#include "Framework/Graphics/Buffer.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cstring>

namespace NS::Graphics
{
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

    BufferDesc MakeVertexBufferDesc(const void* data,
                                    std::size_t vertexCount,
                                    std::size_t stride,
                                    BufferUsage usage) noexcept
    {
        BufferDesc desc{};
        desc.initialData = data;
        desc.byteSize = vertexCount * stride;
        desc.stride = stride;
        desc.usage = usage;
        desc.bindFlags = D3D11_BIND_VERTEX_BUFFER;
        return desc;
    }

    BufferDesc MakeIndexBufferDesc(const void* data,
                                   std::size_t indexCount,
                                   IndexFormat format,
                                   BufferUsage usage) noexcept
    {
        const std::size_t elementSize = (format == IndexFormat::UInt16) ? 2u : 4u;
        BufferDesc desc{};
        desc.initialData = data;
        desc.byteSize = indexCount * elementSize;
        desc.indexFormat = format;
        desc.usage = usage;
        desc.bindFlags = D3D11_BIND_INDEX_BUFFER;
        return desc;
    }

    BufferDesc MakeConstantBufferDesc(std::size_t byteSize) noexcept
    {
        BufferDesc desc{};
        desc.byteSize = byteSize; // 16-byte 切り上げは Buffer ctor が行う
        desc.usage = BufferUsage::Dynamic;
        desc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return desc;
    }

    Buffer::Buffer(Renderer& renderer, const BufferDesc& desc)
    {
        const bool isConstant = (desc.bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0u;
        const std::size_t bytes =
            isConstant ? RoundUpToAlignment(desc.byteSize, kConstantBufferAlignment) : desc.byteSize;

        m_stride = desc.stride;
        m_byteSize = bytes;
        m_format = desc.indexFormat;
        m_usage = desc.usage;

        auto* device = detail::GetDevice(renderer);
        if (device == nullptr || bytes == 0u)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Buffer 構築失敗 (device={}, bytes={}, bind=0x{:X})",
                         static_cast<const void*>(device),
                         bytes,
                         static_cast<unsigned>(desc.bindFlags));
            return;
        }
        if (desc.usage == BufferUsage::Static && desc.initialData == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Static バッファは initialData 必須 (CreateBuffer 後 GPU が未初期化を読む危険)");
            return;
        }

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(bytes);
        bd.BindFlags = desc.bindFlags;
        if (desc.usage == BufferUsage::Dynamic)
        {
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        else
        {
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.CPUAccessFlags = 0;
        }

        D3D11_SUBRESOURCE_DATA srd{};
        D3D11_SUBRESOURCE_DATA* srdPtr = nullptr;
        if (desc.initialData != nullptr)
        {
            srd.pSysMem = desc.initialData;
            srdPtr = &srd;
        }

        const HRESULT hr = device->CreateBuffer(&bd, srdPtr, m_buffer.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "CreateBuffer 失敗 (hr=0x{:X}, bytes={}, bind=0x{:X})",
                         static_cast<unsigned>(hr),
                         bytes,
                         static_cast<unsigned>(desc.bindFlags));
            m_buffer.Reset();
        }
    }

    bool Buffer::IsValid() const noexcept
    {
        return m_buffer != nullptr;
    }
    ID3D11Buffer* Buffer::Native() const noexcept
    {
        return m_buffer.Get();
    }
    std::size_t Buffer::Stride() const noexcept
    {
        return m_stride;
    }
    std::size_t Buffer::ByteSize() const noexcept
    {
        return m_byteSize;
    }
    IndexFormat Buffer::Format() const noexcept
    {
        return m_format;
    }
    BufferUsage Buffer::Usage() const noexcept
    {
        return m_usage;
    }

    namespace detail
    {
        void BindVertexBuffer(ID3D11DeviceContext* context, Buffer& buffer, unsigned slot) noexcept
        {
            if (context == nullptr || !buffer.IsValid())
            {
                return;
            }
            ID3D11Buffer* buffers[1] = {buffer.Native()};
            const UINT stride = static_cast<UINT>(buffer.Stride());
            const UINT offset = 0u;
            context->IASetVertexBuffers(slot, 1u, buffers, &stride, &offset);
        }

        void BindIndexBuffer(ID3D11DeviceContext* context, Buffer& buffer) noexcept
        {
            if (context == nullptr || !buffer.IsValid())
            {
                return;
            }
            context->IASetIndexBuffer(buffer.Native(), ToDxgiFormat(buffer.Format()), 0u);
        }

        void BindConstantBuffer(ID3D11DeviceContext* context,
                                Buffer& buffer,
                                unsigned slot,
                                ShaderStage stages) noexcept
        {
            if (context == nullptr || !buffer.IsValid())
            {
                return;
            }
            ID3D11Buffer* buffers[1] = {buffer.Native()};
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

        void UpdateBufferRaw(ID3D11DeviceContext* context, Buffer& buffer, const void* data, std::size_t bytes) noexcept
        {
            if (context == nullptr || !buffer.IsValid())
            {
                return;
            }
            if (buffer.Usage() != BufferUsage::Dynamic)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Static Buffer への Update は無効 (Dynamic で再構築するか別バッファを使う)");
                return;
            }
            if (bytes > buffer.ByteSize())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Buffer::Update のサイズ超過 (req={}, max={})",
                             bytes,
                             buffer.ByteSize());
                return;
            }
            MapAndCopy(context, buffer.Native(), data, bytes);
        }
    } // namespace detail

} // namespace NS::Graphics
