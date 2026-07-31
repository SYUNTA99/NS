#include "Runtime/Graphics/Buffer.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/detail/D3dUsage.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Renderer.h"

namespace NS::Graphics
{
    namespace
    {
        constexpr std::size_t k_ConstantBufferAlignment = 16;

        [[nodiscard]] std::size_t RoundUpToAlignment(std::size_t bytes, std::size_t alignment) noexcept
        {
            return (bytes + alignment - 1u) & ~(alignment - 1u);
        }
    } // namespace

    BufferDesc MakeVertexBufferDesc(const void* data,
                                    std::size_t vertexCount,
                                    std::size_t stride,
                                    D3D11_USAGE usage) noexcept
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
                                   DXGI_FORMAT format,
                                   D3D11_USAGE usage) noexcept
    {
        const std::size_t elementSize = [format]() -> std::size_t {
            if (format == DXGI_FORMAT_R16_UINT)
                return 2u;
            return 4u;
        }();
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
        desc.byteSize = byteSize; // 16 バイト切り上げは Buffer のコンストラクタでやる
        desc.usage = D3D11_USAGE_DYNAMIC;
        desc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return desc;
    }

    std::unique_ptr<Buffer> Buffer::Create(const BufferDesc& desc)
    {
        return std::unique_ptr<Buffer>(new Buffer(desc));
    }

    Buffer::Buffer(const BufferDesc& desc)
    {
        const bool isConstant = (desc.bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0u;
        const std::size_t bytes = [&]() -> std::size_t {
            if (isConstant)
                return RoundUpToAlignment(desc.byteSize, k_ConstantBufferAlignment);
            return desc.byteSize;
        }();

        m_stride = desc.stride;
        m_byteSize = bytes;
        m_format = desc.indexFormat;
        m_dynamic = (detail::GetCpuAccessFlags(desc.usage) & D3D11_CPU_ACCESS_WRITE) != 0u;

        auto* device = Gpu().device;
        if (device == nullptr || bytes == 0u)
        {
            NS_LOG_ERROR(Graphics,
                         "Buffer 構築失敗 (device={}, bytes={}, bind=0x{:X})",
                         static_cast<const void*>(device),
                         bytes,
                         static_cast<unsigned>(desc.bindFlags));
            return;
        }
        if (desc.usage != D3D11_USAGE_DYNAMIC && desc.initialData == nullptr)
        {
            NS_LOG_ERROR(Graphics, "Static バッファは initialData 必須 (CreateBuffer 後 GPU が未初期化を読む危険)");
            return;
        }

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(bytes);
        bd.BindFlags = desc.bindFlags;
        bd.Usage = desc.usage;
        bd.CPUAccessFlags = detail::GetCpuAccessFlags(desc.usage);

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
            NS_LOG_ERROR(Graphics,
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
    DXGI_FORMAT Buffer::Format() const noexcept
    {
        return m_format;
    }
    bool Buffer::IsDynamic() const noexcept
    {
        return m_dynamic;
    }

} // namespace NS::Graphics
