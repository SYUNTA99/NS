#include "NSlib/Gfx/D3DSkeletalMesh.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DGlobal.h"

#include "NSlib/Core/Types.h"

namespace NS
{
    namespace Gfx
    {
        std::unique_ptr<D3DSkeletalMesh> D3DSkeletalMesh::Create(const D3DSkeletalMeshDesc& desc)
        {
            // D3DDevice の無い場 (GPU の無い試し) では、静的なメッシュと同じく ERROR を出さずに作らない
            if (Gpu().device == nullptr || Gpu().context == nullptr)
            {
                return nullptr;
            }
            return std::unique_ptr<D3DSkeletalMesh>(new D3DSkeletalMesh(desc));
        }

        D3DSkeletalMesh::D3DSkeletalMesh(const D3DSkeletalMeshDesc& desc)
        {
            SetVertexLayout(SkinnedInputLayout());

            const bool descValid = (desc.vertices != nullptr && desc.vertexCount != 0u && desc.indices != nullptr &&
                                    desc.indexCount != 0u);
            if (!descValid)
            {
                NS_LOG_ERROR(
                    Graphics,
                    "D3DSkeletalMesh: D3DSkeletalMeshDesc 不正 — IsValid false (vertices={}, vCount={}, indices={}, "
                    "iCount={})",
                    static_cast<const void*>(desc.vertices),
                    desc.vertexCount,
                    static_cast<const void*>(desc.indices),
                    desc.indexCount);
                return;
            }

            if (!BuildGeometry(
                    desc.vertices, desc.vertexCount, sizeof(SkinnedVertex), desc.indices, desc.indexCount, false))
            {
                NS_LOG_ERROR(Graphics,
                             "D3DSkeletalMesh: VertexBuffer / IndexBuffer 構築失敗 — IsValid false (v={}, i={})",
                             desc.vertexCount,
                             desc.indexCount);
                return;
            }

            // バインドポーズ実測箱。アニメ非再生時のフォールバック境界に使う
            SetLocalBounds(AABBFromPositions(&desc.vertices[0].position, desc.vertexCount, sizeof(SkinnedVertex)));
        }

        D3DSkeletalMesh::~D3DSkeletalMesh() = default;

        std::vector<InputElement> D3DSkeletalMesh::SkinnedInputLayout()
        {
            static const std::vector<InputElement> k_Layout = {
                InputElement{
                    "POSITION", InputElementFormat::Float3, static_cast<uint>(offsetof(SkinnedVertex, position))},
                InputElement{"TEXCOORD", InputElementFormat::Float2, static_cast<uint>(offsetof(SkinnedVertex, uv))},
                InputElement{"NORMAL", InputElementFormat::Float3, static_cast<uint>(offsetof(SkinnedVertex, normal))},
                InputElement{
                    "BLENDINDICES", InputElementFormat::UInt4, static_cast<uint>(offsetof(SkinnedVertex, joints))},
                InputElement{
                    "BLENDWEIGHT", InputElementFormat::Float4, static_cast<uint>(offsetof(SkinnedVertex, weights))},
            };
            return k_Layout;
        }
    } // namespace Gfx
} // namespace NS
