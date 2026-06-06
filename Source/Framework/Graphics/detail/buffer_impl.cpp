#include "Framework/Graphics/Buffer.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

namespace NS::Graphics
{
    namespace
    {
        constexpr std::size_t kConstantBufferAlignment = 16;

        [[nodiscard]] std::size_t RoundUpToAlignment(std::size_t bytes, std::size_t alignment) noexcept
        {
            return (bytes + alignment - 1u) & ~(alignment - 1u);
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

} // namespace NS::Graphics
