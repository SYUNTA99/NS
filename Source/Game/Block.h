#pragma once

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshComponent.h"
#include "Framework/Scene/StaticColliderComponent.h"

namespace NS::Graphics
{
    class Material;
    class Mesh;
} // namespace NS::Graphics

/// 静的ブロックの GameObject (固定スロット)
/// MeshComponent + StaticColliderComponent を named member として保有する
/// Mesh / Material は LevelEditorScene が共有し、見た目サイズは Root::SetScale で、
/// 衝突サイズは StaticColliderComponent::SetHalfExtents で同期管理する
class Block : public NS::Scene::GameObject
{
public:
    Block(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material, const NS::Math::Vector3& halfExtents) noexcept;
    ~Block() override = default;

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = delete;
    Block& operator=(Block&&) = delete;

    [[nodiscard]] NS::Scene::MeshComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] NS::Scene::StaticColliderComponent& Collider() noexcept { return m_collider; }

private:
    NS::Scene::MeshComponent m_mesh;
    NS::Scene::StaticColliderComponent m_collider;
};
