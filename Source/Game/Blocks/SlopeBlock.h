#pragma once

/// @file SlopeBlock.h
/// @brief 楔形スロープブロック (45 / 30 / 22.5 / 15 度の 4 種)
///
/// @details MeshRendererComponent + SlopeColliderComponent を GameObject が所有し、参照を member キャッシュする
/// Block (AABB collider 専用) からではなく GameObject を直接派生する
/// Mesh / Material は LevelEditorScene が共有する生ポインタを受け取り、角度別 wedge mesh は scene 側でキャッシュする

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

class SlopeBlock : public NS::Scene::GameObject
{
public:
    SlopeBlock(NS::Graphics::StaticMesh* wedgeMesh,
               NS::Graphics::Material* material,
               float angleDegrees,
               const NS::Math::Vector3& halfExtents) noexcept;
    ~SlopeBlock() override = default;

    SlopeBlock(const SlopeBlock&) = delete;
    SlopeBlock& operator=(const SlopeBlock&) = delete;
    SlopeBlock(SlopeBlock&&) = delete;
    SlopeBlock& operator=(SlopeBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }
    [[nodiscard]] NS::Scene::SlopeColliderComponent& Collider() noexcept { return *m_collider; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
    NS::Scene::SlopeColliderComponent* m_collider = nullptr;
};
