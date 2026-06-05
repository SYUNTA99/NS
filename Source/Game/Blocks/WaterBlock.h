#pragma once

/// @file WaterBlock.h
/// @brief 視覚のみの水ブロック GameObject (placeholder)
///
/// @details MeshRendererComponent のみを保有する非衝突 GameObject。 player は通り抜け可能で、
/// 現状は「半透明テクスチャ + N·L シェーディング」 の見た目だけを提供する
/// 流体物理 / 浮力 / 呼吸 ゲージ等は将来本 GameObject を非侵襲に拡張する想定
/// 物理 collider 系 Component を意図的に持たないことで、 LevelEditorScene の
/// `m_collisionWorld` には登録されず CharacterController の swept AABB 解決を素通りする

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/MeshRendererComponent.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

class WaterBlock : public NS::Scene::GameObject
{
public:
    WaterBlock(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept;
    ~WaterBlock() override = default;

    WaterBlock(const WaterBlock&) = delete;
    WaterBlock& operator=(const WaterBlock&) = delete;
    WaterBlock(WaterBlock&&) = delete;
    WaterBlock& operator=(WaterBlock&&) = delete;

    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept { return *m_mesh; }

private:
    NS::Scene::MeshRendererComponent* m_mesh = nullptr;
};
