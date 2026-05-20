#pragma once

#include "ns/core/math.h"
#include "ns/scene/components/mesh_component.h"
#include "ns/scene/components/static_collider_component.h"
#include "ns/scene/game_object.h"

namespace ns::graphics
{
    class Material;
    class Mesh;
} // namespace ns::graphics

/// 静的ブロックの GameObject ( 固定スロット)。
/// MeshComponent + StaticColliderComponent を named member として保有する。
/// Mesh / Material は MainScene が共有し、見た目サイズは Root::SetScale で、
/// 衝突サイズは StaticColliderComponent::SetHalfExtents で同期管理する。
class Block : public ns::scene::GameObject
{
public:
    Block(ns::graphics::Mesh* mesh, ns::graphics::Material* material, const ns::core::Vector3& halfExtents) noexcept;
    ~Block() override = default;

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = delete;
    Block& operator=(Block&&) = delete;

    [[nodiscard]] ns::scene::MeshComponent& MeshComp() noexcept { return m_mesh; }
    [[nodiscard]] ns::scene::StaticColliderComponent& Collider() noexcept { return m_collider; }

private:
    ns::scene::MeshComponent m_mesh;
    ns::scene::StaticColliderComponent m_collider;
};
