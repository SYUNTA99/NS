#pragma once

/// @file SkeletalMesh.h
/// @brief NS::Graphics::SkeletalMesh — GPU スキニング対象 mesh の型 (実装は段階的に整備中)
///
/// @details skinning runtime (Skeleton / ボーンパレット / skinned VS) は後続で実装する
/// 現段階は skinned 頂点フォーマット (SkinnedVertex) と構築パラメータ (SkinnedMeshDesc) を定義し、
/// クラス本体は基底 Mesh の派生 slot を確保するためのプレースホルダ
/// geometry 未設定のため IsValid は false / Draw は no-op となる

#include "Framework/Graphics/Mesh.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace NS::Graphics
{
    /// スキン付き頂点フォーマット (64 byte 固定)
    /// position/uv/normal に加え 1 頂点あたり最大 4 ボーンの影響 (joints=ボーン index, weights=重み) を持つ
    /// joints/weights は GPU 入力レイアウト (BLENDINDICES=uint4 / BLENDWEIGHT=float4) と byte 単位で
    /// 一致させるため固定長配列で保持する
    struct SkinnedVertex
    {
        NS::Math::Vector3 position; // offset 0  POSITION
        NS::Math::Vector2 uv;       // offset 12 TEXCOORD
        NS::Math::Vector3 normal;   // offset 20 NORMAL
        std::uint32_t joints[4];    // offset 32 BLENDINDICES
        float weights[4];           // offset 48 BLENDWEIGHT
    };
    static_assert(sizeof(SkinnedVertex) == 64, "SkinnedVertex は 64 byte 固定");
    static_assert(std::is_standard_layout_v<SkinnedVertex>,
                  "SkinnedVertex は offsetof 使用のため標準レイアウト必須 (SkinnedInputLayout)");

    /// SkeletalMesh 構築パラメータ。 Static Buffer 前提で initialData はコンストラクタ内でコピーされる
    /// Index は uint32_t、 boneCount はボーンパレットの有効要素数 (上限 kMaxBones)
    struct SkinnedMeshDesc
    {
        const SkinnedVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0;
        std::size_t boneCount = 0;
    };

    /// GPU スキニング対象 mesh (skinning runtime は後続、 現状はプレースホルダ)
    class SkeletalMesh : public Mesh
    {
    public:
        SkeletalMesh() = default;
        ~SkeletalMesh() override = default;

        SkeletalMesh(const SkeletalMesh&) = delete;
        SkeletalMesh& operator=(const SkeletalMesh&) = delete;
        SkeletalMesh(SkeletalMesh&&) = delete;
        SkeletalMesh& operator=(SkeletalMesh&&) = delete;
    };

} // namespace NS::Graphics
