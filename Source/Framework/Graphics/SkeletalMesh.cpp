#include "Framework/Graphics/SkeletalMesh.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace NS::Graphics
{
    namespace
    {
        // skinned.vs.hlsl の cbuffer BonePalette : register(b1) に対応
        constexpr unsigned kBonePaletteSlot = 1;

        void FillIdentity(BonePaletteCB& cb) noexcept
        {
            for (std::size_t i = 0; i < kMaxBones; ++i)
            {
                cb.bones[i] = NS::Math::Matrix::Identity;
            }
        }

        bool BuildBuffers(const SkinnedVertex* vertices,
                          std::size_t vertexCount,
                          const std::uint32_t* indices,
                          std::size_t indexCount,
                          std::unique_ptr<Buffer>& outVb,
                          std::unique_ptr<Buffer>& outIb)
        {
            BufferDesc vbDesc = MakeVertexBufferDesc(vertices, vertexCount, sizeof(SkinnedVertex));
            std::unique_ptr<Buffer> vb = Buffer::Create(vbDesc);
            if (!vb->IsValid())
                return false;

            BufferDesc ibDesc = MakeIndexBufferDesc(indices, indexCount, DXGI_FORMAT_R32_UINT);
            std::unique_ptr<Buffer> ib = Buffer::Create(ibDesc);
            if (!ib->IsValid())
                return false;

            outVb = std::move(vb);
            outIb = std::move(ib);
            return true;
        }
    } // namespace

    std::unique_ptr<SkeletalMesh> SkeletalMesh::Create(const SkinnedMeshDesc& desc)
    {
        return std::unique_ptr<SkeletalMesh>(new SkeletalMesh(desc));
    }

    SkeletalMesh::SkeletalMesh(const SkinnedMeshDesc& desc)
    {
        if (Gpu().device == nullptr || Gpu().context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "SkeletalMesh: Renderer の Device / Context が無効");
            return;
        }

        SetVertexLayout(SkinnedInputLayout());

        const bool descValid =
            (desc.vertices != nullptr && desc.vertexCount != 0u && desc.indices != nullptr && desc.indexCount != 0u);
        if (!descValid)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh: SkinnedMeshDesc 不正 — IsValid false (vertices={}, vCount={}, indices={}, "
                         "iCount={})",
                         static_cast<const void*>(desc.vertices),
                         desc.vertexCount,
                         static_cast<const void*>(desc.indices),
                         desc.indexCount);
            return;
        }

        std::unique_ptr<Buffer> vb;
        std::unique_ptr<Buffer> ib;
        if (!BuildBuffers(desc.vertices, desc.vertexCount, desc.indices, desc.indexCount, vb, ib))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh: VertexBuffer / IndexBuffer 構築失敗 — IsValid false (v={}, i={})",
                         desc.vertexCount,
                         desc.indexCount);
            return;
        }
        SetGeometry(std::move(vb), std::move(ib), desc.vertexCount, desc.indexCount, false);

        if (desc.boneCount > kMaxBones)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh: boneCount {} が上限 {} を超過 — 上限に切詰め",
                         desc.boneCount,
                         kMaxBones);
        }
        m_boneCount = (desc.boneCount < kMaxBones) ? desc.boneCount : kMaxBones;

        // 既定は恒等パレットで初期化し、 pose 未指定でも bind pose 相当で描画できるようにする
        FillIdentity(m_palette);
        BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(BonePaletteCB));
        std::unique_ptr<Buffer> cb = Buffer::Create(cbDesc);
        if (cb->IsValid())
        {
            m_bonePaletteCB = std::move(cb);
        }
        else
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "SkeletalMesh: bone palette ConstantBuffer 構築失敗");
        }
    }

    SkeletalMesh::~SkeletalMesh() = default;

    void SkeletalMesh::SetBonePalette(std::span<const NS::Math::Matrix> palette) noexcept
    {
        if (!m_bonePaletteCB)
            return;

        FillIdentity(m_palette);
        const std::size_t count = (palette.size() < kMaxBones) ? palette.size() : kMaxBones;
        for (std::size_t i = 0; i < count; ++i)
        {
            m_palette.bones[i] = palette[i];
        }
        if (palette.size() > kMaxBones)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh::SetBonePalette: palette {} 個が上限 {} を超過 — 上限まで反映",
                         palette.size(),
                         kMaxBones);
        }
    }

    void SkeletalMesh::Draw(Renderer& renderer) noexcept
    {
        if (!IsValid())
            return;
        if (m_bonePaletteCB)
        {
            renderer.Commands().UpdateBuffer(*m_bonePaletteCB, &m_palette, sizeof(BonePaletteCB));
            renderer.Commands().SetConstantBuffer(*m_bonePaletteCB, kBonePaletteSlot, ShaderType::Vertex);
        }
        Mesh::Draw(renderer);
    }

    std::vector<InputElement> SkeletalMesh::SkinnedInputLayout()
    {
        static const std::vector<InputElement> kLayout = {
            InputElement{
                "POSITION", InputElementFormat::Float3, static_cast<unsigned>(offsetof(SkinnedVertex, position))},
            InputElement{"TEXCOORD", InputElementFormat::Float2, static_cast<unsigned>(offsetof(SkinnedVertex, uv))},
            InputElement{"NORMAL", InputElementFormat::Float3, static_cast<unsigned>(offsetof(SkinnedVertex, normal))},
            InputElement{
                "BLENDINDICES", InputElementFormat::UInt4, static_cast<unsigned>(offsetof(SkinnedVertex, joints))},
            InputElement{
                "BLENDWEIGHT", InputElementFormat::Float4, static_cast<unsigned>(offsetof(SkinnedVertex, weights))},
        };
        return kLayout;
    }

    std::size_t SkeletalMesh::BoneCount() const noexcept
    {
        return m_boneCount;
    }

} // namespace NS::Graphics
