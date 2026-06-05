#pragma once

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshRendererComponent.h"
#include "Framework/Scene/StaticColliderComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

/// 静的ブロックの GameObject
/// MeshRendererComponent + StaticColliderComponent を GameObject が所有し、参照を member キャッシュする
/// Mesh / Material は LevelEditorScene が共有し、見た目サイズは Root::SetScale で、
/// 衝突サイズは StaticColliderComponent::SetHalfExtents で同期管理する
class Block : public NS::Scene::GameObject
{
public:
    Block(NS::Graphics::StaticMesh* mesh,
          NS::Graphics::Material* material,
          const NS::Math::Vector3& halfExtents) noexcept;
    ~Block() override = default;

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = delete;
    Block& operator=(Block&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }
    [[nodiscard]] NS::Scene::StaticColliderComponent& Collider() noexcept { return *m_collider; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
    NS::Scene::StaticColliderComponent* m_collider = nullptr;
};
