#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DShader.h"

#include <span>

namespace NS::Gfx
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
                                 "D3DMesh::CreateInputLayout: 引数不正 (device={}, vsBytecode={}, vsSize={}, elements={})",
                                 static_cast<const void*>(device),
                                 vsBytecode,
                                 vsBytecodeSize,
                                 elements.size());
                    return false;
                }
                // InputElement を D3D11_INPUT_ELEMENT_DESC へ詰め替える
                std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
                descs.reserve(elements.size());
                for (const InputElement& e : elements)
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
                const HRESULT hr = device->CreateInputLayout(descs.data(),
                                                             static_cast<UINT>(descs.size()),
                                                             vsBytecode,
                                                             vsBytecodeSize,
                                                             outLayout.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics,
                                 "D3DMesh::CreateInputLayout: CreateInputLayout 失敗 (hr=0x{:X}, elements={})",
                                 static_cast<uint>(hr),
                                 elements.size());
                    return false;
                }
                return true;
            }
        } // namespace

        D3DMesh::D3DMesh() = default;
        D3DMesh::~D3DMesh() = default;

        bool D3DMesh::BuildGeometry(const void* vertices,
                                 std::size_t vertexCount,
                                 std::size_t stride,
                                 const uint32* indices,
                                 std::size_t indexCount,
                                 bool usingFallback) noexcept
        {
            std::unique_ptr<D3DBuffer> vb = D3DBuffer::Create(MakeVertexBufferDesc(vertices, vertexCount, stride));
            if (!vb->IsValid())
            {
                return false;
            }
            std::unique_ptr<D3DBuffer> ib = D3DBuffer::Create(MakeIndexBufferDesc(indices, indexCount, DXGI_FORMAT_R32_UINT));
            if (!ib->IsValid())
            {
                return false;
            }
            m_vb = std::move(vb);
            m_ib = std::move(ib);
            m_vertexCount = vertexCount;
            m_indexCount = indexCount;
            m_usingFallback = usingFallback;
            m_valid = (m_vb != nullptr && m_ib != nullptr && Gpu().device != nullptr);
            return true;
        }

        void D3DMesh::SetVertexLayout(std::vector<InputElement> elements) noexcept
        {
            m_layoutElements = std::move(elements);
        }

        void D3DMesh::SetTopology(Topology topology) noexcept { m_topology = topology; }

        void D3DMesh::CreateInputLayout(const D3DShader& vertexShader) noexcept
        {
            ShaderInputLayout* entry = nullptr;
            for (ShaderInputLayout& candidate : m_inputLayouts)
            {
                if (candidate.shader == &vertexShader)
                {
                    entry = &candidate;
                    break;
                }
            }
            // 同じ番地でも番号が違えば、読み直したか、捨てた後に作られた別のシェーダー
            if (entry != nullptr && entry->bytecodeId == vertexShader.BytecodeId())
            {
                m_selectedLayout = entry->layout.Get();
                return;
            }

            ID3D11Device* device = Gpu().device;
            if (device == nullptr || m_layoutElements.empty())
            {
                NS_LOG_ERROR(Graphics, "D3DMesh::CreateInputLayout: device が無効か、レイアウト要素が空");
                return;
            }
            if (entry == nullptr)
            {
                entry = &m_inputLayouts.emplace_back();
                entry->shader = &vertexShader;
            }
            entry->bytecodeId = vertexShader.BytecodeId();
            entry->layout.Reset();
            m_selectedLayout = nullptr;

            const std::span<const std::byte> bytecode = vertexShader.VertexShaderBytecode();
            if (bytecode.empty())
            {
                NS_LOG_ERROR(Graphics, "D3DMesh::CreateInputLayout: VS バイトコードが空");
                return;
            }
            if (CreateInputLayoutFromDesc(device, bytecode.data(), bytecode.size(), m_layoutElements, entry->layout))
            {
                m_selectedLayout = entry->layout.Get();
            }
        }

        bool D3DMesh::IsValid() const noexcept { return m_valid; }
        bool D3DMesh::IsUsingFallback() const noexcept { return m_usingFallback; }
        std::size_t D3DMesh::VertexCount() const noexcept { return m_vertexCount; }
        std::size_t D3DMesh::IndexCount() const noexcept { return m_indexCount; }
        const AABB& D3DMesh::LocalBounds() const noexcept { return m_localBounds; }
        void D3DMesh::SetLocalBounds(const AABB& bounds) noexcept { m_localBounds = bounds; }
        Topology D3DMesh::GetTopology() const noexcept { return m_topology; }

        void DrawMesh(D3DCommandList& commands, const D3DMesh& mesh) noexcept
        {
            if (!mesh.IsValid())
            {
                return;
            }

            commands.SetInputLayout(mesh.InputLayout());
            commands.SetVertexBuffer(*mesh.VertexBuffer(), 0);
            commands.SetIndexBuffer(*mesh.IndexBuffer());
            commands.SetTopology(mesh.GetTopology());
            commands.DrawIndexed(static_cast<uint>(mesh.IndexCount()));
        }

        const D3DBuffer* D3DMesh::VertexBuffer() const noexcept { return m_vb.get(); }
        const D3DBuffer* D3DMesh::IndexBuffer() const noexcept { return m_ib.get(); }
        ID3D11InputLayout* D3DMesh::InputLayout() const noexcept { return m_selectedLayout; }
} // namespace NS::Gfx
