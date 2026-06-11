#pragma once

/// @file DecorationBlock.h
/// @brief 装飾用の非衝突 GameObject
///
/// @details MeshRendererComponent のみ保有し物理 collider 系 Component を持たないため、 player は
/// 完全に通り抜けられる。 「装飾を Block 派生にすると衝突がくっついてくる」 を
/// 設計レベルで回避するため、 GameObject から直接派生して collider Component を意図的に
/// 省く構造を取る。 LevelEditorScene 側の collider 集約からも自動的に外れる

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

class DecorationBlock : public NS::Scene::GameObject
{
public:
    DecorationBlock(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept;
    ~DecorationBlock() override = default;

    DecorationBlock(const DecorationBlock&) = delete;
    DecorationBlock& operator=(const DecorationBlock&) = delete;
    DecorationBlock(DecorationBlock&&) = delete;
    DecorationBlock& operator=(DecorationBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
};
