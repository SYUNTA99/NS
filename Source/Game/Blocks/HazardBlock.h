#pragma once

/// @file HazardBlock.h
/// @brief 接触ダメージ付きの固形ブロック GameObject
///
/// @details MeshRendererComponent (cube mesh) + StaticColliderComponent (AABB 衝突) +
/// HazardComponent (ダメージ trigger) を named member として保有する
/// 固形挙動は通常 Block と同じだが、 player capsule が AABB と overlap した frame で
/// HazardComponent::OnPlayerOverlap が呼ばれ PlayState.playerHealth を 1 削る
/// Block 派生にせず GameObject を直接派生して 3 つの Component を明示保有する
/// 描画は SlopeBlock / PoleBlock と同じく per-block MeshRendererComponent::Draw 経路

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/HazardComponent.h"
#include "Framework/Scene/MeshRendererComponent.h"
#include "Framework/Scene/StaticColliderComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

class HazardBlock : public NS::Scene::GameObject
{
public:
    HazardBlock(NS::Graphics::StaticMesh* mesh,
                NS::Graphics::Material* material,
                const NS::Math::Vector3& halfExtents) noexcept;
    ~HazardBlock() override = default;

    HazardBlock(const HazardBlock&) = delete;
    HazardBlock& operator=(const HazardBlock&) = delete;
    HazardBlock(HazardBlock&&) = delete;
    HazardBlock& operator=(HazardBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] NS::Scene::StaticColliderComponent& Collider() noexcept { return m_collider; }
    [[nodiscard]] NS::Scene::HazardComponent& Hazard() noexcept { return m_hazard; }

private:
    NS::Scene::MeshRendererComponent m_mesh;
    NS::Scene::StaticColliderComponent m_collider;
    NS::Scene::HazardComponent m_hazard;
};
