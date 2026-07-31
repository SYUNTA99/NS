#include "Runtime/Graphics/Mesh.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/CommandList.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Shader.h"

#include <span>

namespace NS::Graphics
{

    namespace
    {
        [[nodiscard]] DXGI_FORMAT ToDxgiFormat(InputElementFormat fmt) noexcept
        {
            switch (fmt)
            {
            case InputElementFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case InputElementFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case InputElementFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case InputElementFormat::UInt32:
                return DXGI_FORMAT_R32_UINT;
            case InputElementFormat::UInt4:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        bool CreateInputLayoutFromDesc(ID3D11Device* device,
                                       const void* vsBytecode,
                                       std::size_t vsBytecodeSize,
                                       const std::vector<InputElement>& elements,
                                       ComPtr<ID3D11InputLayout>& outLayout) noexcept
        {
            if (device == nullptr || vsBytecode == nullptr || vsBytecodeSize == 0u || elements.empty())
            {
                NS_LOG_ERROR(Graphics,
                             "Mesh::CreateInputLayout: 引数不正 (device={}, vsBytecode={}, vsSize={}, elements={})",
                             static_cast<const void*>(device),
                             vsBytecode,
                             vsBytecodeSize,
                             elements.size());
                return false;
            }
            // InputElement を D3D11_INPUT_ELEMENT_DESC へ詰め替える
            std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
            descs.reserve(elements.size());
            for (const auto& e : elements)
            {
                D3D11_INPUT_ELEMENT_DESC d{};
                d.SemanticName = e.semanticName.c_str();
                d.SemanticIndex = 0u;
                d.Format = ToDxgiFormat(e.format);
                d.InputSlot = 0u;
                d.AlignedByteOffset = e.byteOffset;
                d.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                d.InstanceDataStepRate = 0u;
                descs.push_back(d);
            }
            const HRESULT hr = device->CreateInputLayout(
                descs.data(), static_cast<UINT>(descs.size()), vsBytecode, vsBytecodeSize, outLayout.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics,
                             "Mesh::CreateInputLayout: CreateInputLayout 失敗 (hr=0x{:X}, elements={})",
                             static_cast<unsigned>(hr),
                             elements.size());
                return false;
            }
            return true;
        }
    } // namespace

    Mesh::Mesh() = default;
    Mesh::~Mesh() = default;

    void Mesh::SetGeometry(std::unique_ptr<Buffer> vertexBuffer,
                           std::unique_ptr<Buffer> indexBuffer,
                           std::size_t vertexCount,
                           std::size_t indexCount,
                           bool usingFallback) noexcept
    {
        m_vb = std::move(vertexBuffer);
        m_ib = std::move(indexBuffer);
        m_vertexCount = vertexCount;
        m_indexCount = indexCount;
        m_usingFallback = usingFallback;
        m_valid = (m_vb != nullptr && m_ib != nullptr && Gpu().device != nullptr);
    }

    void Mesh::SetVertexLayout(std::vector<InputElement> elements) noexcept
    {
        m_layoutElements = std::move(elements);
    }

    void Mesh::SetTopology(Topology topology) noexcept
    {
        m_topology = topology;
    }

    void Mesh::CreateInputLayout(const Shader& vertexShader) noexcept
    {
        if (m_inputLayout)
            return;
        ID3D11Device* device = Gpu().device;
        if (device == nullptr || m_layoutElements.empty())
            return;

        const std::span<const std::byte> bytecode = detail::GetVertexShaderBytecode(vertexShader);
        if (bytecode.empty())
        {
            NS_LOG_ERROR(Graphics, "Mesh::CreateInputLayout: VS バイトコードが空");
            return;
        }

        ComPtr<ID3D11InputLayout> layout;
        if (CreateInputLayoutFromDesc(device, bytecode.data(), bytecode.size(), m_layoutElements, layout))
        {
            m_inputLayout = std::move(layout);
        }
    }

    bool Mesh::IsValid() const noexcept
    {
        return m_valid;
    }
    bool Mesh::IsUsingFallback() const noexcept
    {
        return m_usingFallback;
    }
    std::size_t Mesh::VertexCount() const noexcept
    {
        return m_vertexCount;
    }
    std::size_t Mesh::IndexCount() const noexcept
    {
        return m_indexCount;
    }

    const NS::Math::AABB& Mesh::LocalBounds() const noexcept
    {
        return m_localBounds;
    }

    void Mesh::SetLocalBounds(const NS::Math::AABB& bounds) noexcept
    {
        m_localBounds = bounds;
    }

    Topology Mesh::GetTopology() const noexcept
    {
        return m_topology;
    }

    void DrawMesh(CommandList& commands, const Mesh& mesh) noexcept
    {
        if (!mesh.IsValid())
            return;

        commands.SetInputLayout(mesh.InputLayout());
        commands.SetVertexBuffer(*mesh.VertexBuffer(), 0);
        commands.SetIndexBuffer(*mesh.IndexBuffer());
        commands.SetTopology(mesh.GetTopology());
        commands.DrawIndexed(static_cast<unsigned>(mesh.IndexCount()));
    }

    const Buffer* Mesh::VertexBuffer() const noexcept
    {
        return m_vb.get();
    }

    const Buffer* Mesh::IndexBuffer() const noexcept
    {
        return m_ib.get();
    }

    ID3D11InputLayout* Mesh::InputLayout() const noexcept
    {
        return m_inputLayout.Get();
    }

} // namespace NS::Graphics
