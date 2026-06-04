#pragma once

/// @file SkeletalMesh.h
/// @brief NS::Graphics::SkeletalMesh — GPU スキニング対象 mesh の型 (現状は空ガワ)
///
/// @details skinning runtime (SkinnedVertex / Skeleton / ボーンパレット / skinned VS) は後続で実装する
/// 現段階は基底 Mesh の派生 slot を確保するためのプレースホルダで、 geometry 未設定のため
/// IsValid は false / Draw は no-op となる。 実装時は SetGeometry で skinned VB を預け、
/// Draw を override して bone palette CB の bind を足す

#include "Framework/Graphics/Mesh.h"

namespace NS::Graphics
{
    /// GPU スキニング対象 mesh (現状はプレースホルダ、 skinning runtime は後続)
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
