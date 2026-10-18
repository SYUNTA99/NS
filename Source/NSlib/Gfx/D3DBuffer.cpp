#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/detail/D3dUsage.h"

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            constexpr std::size_t k_ConstantBufferAlignment = 16;

            [[nodiscard]] std::size_t RoundUpToAlignment(std::size_t bytes, std::size_t alignment) noexcept
            {
                return (bytes + alignment - 1u) & ~(alignment - 1u);
            }
        } // namespace

        D3DBufferDesc MakeVertexBufferDesc(const void* data,
                                        std::size_t vertexCount,
                                        std::size_t stride,
                                        D3D11_USAGE usage) noexcept
        {
            D3DBufferDesc desc{};
            desc.initialData = data;
            desc.byteSize = vertexCount * stride;
            desc.stride = stride;
            desc.usage = usage;
            desc.bindFlags = D3D11_BIND_VERTEX_BUFFER;

            return desc;
        }

        D3DBufferDesc MakeIndexBufferDesc(const void* data,
                                       std::size_t indexCount,
                                       DXGI_FORMAT format,
                                       D3D11_USAGE usage) noexcept
        {
            const std::size_t elementSize = [format]() -> std::size_t {
                if (format == DXGI_FORMAT_R16_UINT)
                {
                    return 2u;
                }
                return 4u;
            }();
            D3DBufferDesc desc{};
            desc.initialData = data;
            desc.byteSize = indexCount * elementSize;
            desc.indexFormat = format;
            desc.usage = usage;
            desc.bindFlags = D3D11_BIND_INDEX_BUFFER;

            return desc;
        }

        D3DBufferDesc MakeConstantBufferDesc(std::size_t byteSize) noexcept
        {
            D3DBufferDesc desc{};
            desc.byteSize = byteSize; // 16 バイト切り上げは D3DBuffer のコンストラクタでやる
            desc.usage = D3D11_USAGE_DYNAMIC;
            desc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;

            return desc;
        }

        std::unique_ptr<D3DBuffer> D3DBuffer::Create(const D3DBufferDesc& desc)
        {
            return std::unique_ptr<D3DBuffer>(new D3DBuffer(desc));
        }

        D3DBuffer::D3DBuffer(const D3DBufferDesc& desc)
        {
            const bool isConstant = (desc.bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0u;
            const std::size_t bytes = [&]() -> std::size_t {
                if (isConstant)
                {
                    return RoundUpToAlignment(desc.byteSize, k_ConstantBufferAlignment);
                }
                return desc.byteSize;
            }();

            m_stride = desc.stride;
            m_byteSize = bytes;
            m_format = desc.indexFormat;
            m_dynamic = (detail::GetCpuAccessFlags(desc.usage) & D3D11_CPU_ACCESS_WRITE) != 0u;

            ID3D11Device* device = Gpu().device;
            if (device == nullptr || bytes == 0u)
            {
                NS_LOG_ERROR(Graphics,
                             "D3DBuffer 構築失敗 (device={}, bytes={}, bind=0x{:X})",
                             static_cast<const void*>(device),
                             bytes,
                             static_cast<uint>(desc.bindFlags));
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
                             static_cast<uint>(hr),
                             bytes,
                             static_cast<uint>(desc.bindFlags));
                m_buffer.Reset();
            }
        }

        bool D3DBuffer::IsValid() const noexcept
        {
            return m_buffer != nullptr;
        }
        ID3D11Buffer* D3DBuffer::Native() const noexcept
        {
            return m_buffer.Get();
        }
        std::size_t D3DBuffer::Stride() const noexcept
        {
            return m_stride;
        }
        std::size_t D3DBuffer::ByteSize() const noexcept
        {
            return m_byteSize;
        }
        DXGI_FORMAT D3DBuffer::Format() const noexcept
        {
            return m_format;
        }
        bool D3DBuffer::IsDynamic() const noexcept
        {
            return m_dynamic;
        }
    } // namespace Gfx
} // namespace NS
