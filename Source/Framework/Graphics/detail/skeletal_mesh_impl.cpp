#include "Framework/Graphics/SkeletalMesh.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

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
        // 単一キャラ向けのパレット上限。 128 * 64byte = 8KB で D3D11 CB 上限 (64KB) 内に収まる
        constexpr std::size_t kMaxBones = 128;
        // skinned.vs.hlsl の cbuffer BonePalette : register(b1) に対応
        constexpr unsigned kBonePaletteSlot = 1;

        struct alignas(16) BonePaletteCB
        {
            NS::Math::Matrix bones[kMaxBones];
        };
        static_assert((sizeof(BonePaletteCB) % 16) == 0, "BonePaletteCB は 16 byte 倍数 (ConstantBuffer::Update 要件)");

        void FillIdentity(BonePaletteCB& cb) noexcept
        {
            for (std::size_t i = 0; i < kMaxBones; ++i)
            {
                cb.bones[i] = NS::Math::Matrix::Identity;
            }
        }

        bool BuildBuffers(Renderer& renderer,
                          const SkinnedVertex* vertices,
                          std::size_t vertexCount,
                          const std::uint32_t* indices,
                          std::size_t indexCount,
                          std::unique_ptr<VertexBuffer>& outVb,
                          std::unique_ptr<IndexBuffer>& outIb)
        {
            VertexBufferDesc vbd{};
            vbd.initialData = vertices;
            vbd.vertexCount = vertexCount;
            vbd.stride = sizeof(SkinnedVertex);
            vbd.usage = BufferUsage::Static;
            auto vb = std::make_unique<VertexBuffer>(renderer, vbd);
            if (!vb->IsValid())
                return false;

            IndexBufferDesc ibd{};
            ibd.initialData = indices;
            ibd.indexCount = indexCount;
            ibd.format = IndexFormat::UInt32;
            ibd.usage = BufferUsage::Static;
            auto ib = std::make_unique<IndexBuffer>(renderer, ibd);
            if (!ib->IsValid())
                return false;

            outVb = std::move(vb);
            outIb = std::move(ib);
            return true;
        }
    } // namespace

    struct SkeletalMesh::Impl
    {
        std::unique_ptr<ConstantBuffer> bonePaletteCB;
        std::size_t boneCount = 0;
    };

    SkeletalMesh::SkeletalMesh(Renderer& renderer, const SkinnedMeshDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        if (detail::GetDevice(renderer) == nullptr || detail::GetContext(renderer) == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "SkeletalMesh: Renderer の Device / Context が無効");
            return;
        }

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

        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<IndexBuffer> ib;
        if (!BuildBuffers(renderer, desc.vertices, desc.vertexCount, desc.indices, desc.indexCount, vb, ib))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh: VertexBuffer / IndexBuffer 構築失敗 — IsValid false (v={}, i={})",
                         desc.vertexCount,
                         desc.indexCount);
            return;
        }
        SetGeometry(renderer, std::move(vb), std::move(ib), desc.vertexCount, desc.indexCount, false);

        if (desc.boneCount > kMaxBones)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh: boneCount {} が上限 {} を超過 — 上限に切詰め",
                         desc.boneCount,
                         kMaxBones);
        }
        m_pImpl->boneCount = (desc.boneCount < kMaxBones) ? desc.boneCount : kMaxBones;

        // 既定は恒等パレットで初期化し、 pose 未指定でも bind pose 相当で描画できるようにする
        auto cb = std::make_unique<ConstantBuffer>(renderer, sizeof(BonePaletteCB));
        if (cb->IsValid())
        {
            BonePaletteCB initial;
            FillIdentity(initial);
            cb->Update(initial);
            m_pImpl->bonePaletteCB = std::move(cb);
        }
        else
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "SkeletalMesh: bone palette ConstantBuffer 構築失敗");
        }
    }

    SkeletalMesh::~SkeletalMesh() = default;

    void SkeletalMesh::SetBonePalette(std::span<const NS::Math::Matrix> palette) noexcept
    {
        if (!m_pImpl || !m_pImpl->bonePaletteCB)
            return;

        BonePaletteCB cb;
        FillIdentity(cb);
        const std::size_t count = (palette.size() < kMaxBones) ? palette.size() : kMaxBones;
        for (std::size_t i = 0; i < count; ++i)
        {
            cb.bones[i] = palette[i];
        }
        if (palette.size() > kMaxBones)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "SkeletalMesh::SetBonePalette: palette {} 個が上限 {} を超過 — 上限まで反映",
                         palette.size(),
                         kMaxBones);
        }
        m_pImpl->bonePaletteCB->Update(cb);
    }

    void SkeletalMesh::Draw() noexcept
    {
        if (!IsValid())
            return;
        if (m_pImpl && m_pImpl->bonePaletteCB)
        {
            m_pImpl->bonePaletteCB->Bind(kBonePaletteSlot, ShaderStage::Vertex);
        }
        Mesh::Draw();
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
        return m_pImpl ? m_pImpl->boneCount : 0u;
    }

} // namespace NS::Graphics
