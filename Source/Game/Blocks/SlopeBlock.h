#pragma once

/// @file SlopeBlock.h
/// @brief 楔形スロープブロック (45 / 30 / 22.5 / 15 度の 4 種)
///
/// @details MeshComponent + SlopeColliderComponent を named member として保有する
/// Block (AABB collider 専用) からではなく GameObject を直接派生する
/// Mesh / Material は外部 (LevelEditorScene) が共有してくれた raw pointer を保持し、
/// 角度別 wedge mesh は scene 側でキャッシュする想定

#include "Framework/Core/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshComponent.h"
#include "Framework/Scene/SlopeColliderComponent.h"

namespace NS::Graphics
{
    class Material;
    class Mesh;
} // namespace NS::Graphics

class SlopeBlock : public NS::Scene::GameObject
{
public:
    SlopeBlock(NS::Graphics::Mesh* wedgeMesh,
               NS::Graphics::Material* material,
               float angleDegrees,
               const NS::Core::Vector3& halfExtents) noexcept;
    ~SlopeBlock() override = default;

    SlopeBlock(const SlopeBlock&) = delete;
    SlopeBlock& operator=(const SlopeBlock&) = delete;
    SlopeBlock(SlopeBlock&&) = delete;
    SlopeBlock& operator=(SlopeBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] NS::Scene::SlopeColliderComponent& Collider() noexcept { return m_collider; }

private:
    NS::Scene::MeshComponent m_mesh;
    NS::Scene::SlopeColliderComponent m_collider;
};
