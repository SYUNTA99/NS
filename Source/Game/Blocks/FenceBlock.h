#pragma once

/// @file FenceBlock.h
/// @brief 掴まりフェンス GameObject。
///
/// @details MeshComponent (薄板 mesh) + ClimbableSurfaceComponent (trigger) を named member
/// として保有する。 Block (AABB collider 専用) からではなく GameObject を直接派生する。
/// 半サイズと face normal を ctor で受け取り、 両者の整合は LevelEditorScene 側で確保する。

#include "Framework/Core/Math.h"
#include "Framework/Scene/ClimbableSurfaceComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshComponent.h"

namespace NS::Graphics
{
    class Material;
    class Mesh;
} // namespace NS::Graphics

class FenceBlock : public NS::Scene::GameObject
{
public:
    FenceBlock(NS::Graphics::Mesh* quadMesh,
               NS::Graphics::Material* material,
               const NS::Core::Vector3& halfExtents,
               const NS::Core::Vector3& faceNormal) noexcept;
    ~FenceBlock() override = default;

    FenceBlock(const FenceBlock&) = delete;
    FenceBlock& operator=(const FenceBlock&) = delete;
    FenceBlock(FenceBlock&&) = delete;
    FenceBlock& operator=(FenceBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] NS::Scene::ClimbableSurfaceComponent& Climbable() noexcept { return m_climbable; }

private:
    NS::Scene::MeshComponent m_mesh;
    NS::Scene::ClimbableSurfaceComponent m_climbable;
};
