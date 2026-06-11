#pragma once

#include "Framework/Math/Math.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/StaticColliderComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

/// 静的ブロック。 Mesh / Collider Component を所有し Scale と halfExtents を同期管理する
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
