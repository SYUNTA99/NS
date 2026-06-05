#pragma once

/// @file PoleBlock.h
/// @brief 掴まりポール GameObject
///
/// @details MeshRendererComponent (cylinder mesh) + PoleComponent (trigger) を GameObject が所有し、参照を member
/// キャッシュする Block (AABB collider 専用) からではなく GameObject を直接派生する Mesh / Material は scene 寿命中、
/// 共有 raw pointer として LevelEditorScene が持つ 描画は per-block MeshRendererComponent::Draw 経路 (SlopeBlock
/// と同じ、 InstanceBatcher は cube 専用)

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshRendererComponent.h"
#include "Framework/Scene/PoleComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

class PoleBlock : public NS::Scene::GameObject
{
public:
    PoleBlock(NS::Graphics::StaticMesh* cylinderMesh,
              NS::Graphics::Material* material,
              float radius,
              float height) noexcept;
    ~PoleBlock() override = default;

    PoleBlock(const PoleBlock&) = delete;
    PoleBlock& operator=(const PoleBlock&) = delete;
    PoleBlock(PoleBlock&&) = delete;
    PoleBlock& operator=(PoleBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }
    [[nodiscard]] NS::Scene::PoleComponent& Pole() noexcept { return *m_pole; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
    NS::Scene::PoleComponent* m_pole = nullptr;
};
