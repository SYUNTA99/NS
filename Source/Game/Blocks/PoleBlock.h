#pragma once

/// @file PoleBlock.h
/// @brief 掴まりポール GameObject
///
/// @details MeshComponent (cylinder mesh) + PoleComponent (trigger) を named member として保有する
/// Block (AABB collider 専用) からではなく GameObject を直接派生する
/// Mesh / Material は scene 寿命中、 共有 raw pointer として LevelEditorScene が持つ
/// 描画は per-block MeshComponent::Draw 経路 (SlopeBlock と同じ、 InstanceBatcher は cube 専用)

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshComponent.h"
#include "Framework/Scene/PoleComponent.h"

namespace NS::Graphics
{
    class Material;
    class Mesh;
} // namespace NS::Graphics

class PoleBlock : public NS::Scene::GameObject
{
public:
    PoleBlock(NS::Graphics::Mesh* cylinderMesh, NS::Graphics::Material* material, float radius, float height) noexcept;
    ~PoleBlock() override = default;

    PoleBlock(const PoleBlock&) = delete;
    PoleBlock& operator=(const PoleBlock&) = delete;
    PoleBlock(PoleBlock&&) = delete;
    PoleBlock& operator=(PoleBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] NS::Scene::PoleComponent& Pole() noexcept { return m_pole; }

private:
    NS::Scene::MeshComponent m_mesh;
    NS::Scene::PoleComponent m_pole;
};
