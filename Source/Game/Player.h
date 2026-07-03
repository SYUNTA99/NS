#pragma once

#include "Framework/Scene/Components/CharacterMovementComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PlayerInputComponent.h"
#include "Framework/Scene/Components/ShadowComponent.h"
#include "Framework/Scene/GameObject.h"

namespace NS::Graphics
{
    class Material;
    class StaticMesh;
} // namespace NS::Graphics

/// プレイヤーキャラクタ。 Mesh / Movement / Input / Shadow の既定構成をコードで組み、
/// 追加の component と値は PlayerTuning.json の読込が data から合成する
class Player : public NS::Scene::GameObject
{
public:
    /// 既定構成をデフォルト値で組む。 mesh / material / 影資源はファクトリが data と assets から注入する
    Player() noexcept;
    Player(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept;
    ~Player() override = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    // 既定構成に必ず載る型への short-cut。 参照をキャッシュせず現在の構成から引くので、
    // 構成が data 合成で増えても取り違えない。 既定構成が前提なので戻りは非 null
    [[nodiscard]] NS::Scene::MeshRendererComponent& MeshComp() noexcept
    {
        return *FindComponent<NS::Scene::MeshRendererComponent>();
    }
    [[nodiscard]] NS::Scene::CharacterMovementComponent& Movement() noexcept
    {
        return *FindComponent<NS::Scene::CharacterMovementComponent>();
    }
    [[nodiscard]] NS::Scene::PlayerInputComponent& InputComp() noexcept
    {
        return *FindComponent<NS::Scene::PlayerInputComponent>();
    }
    [[nodiscard]] NS::Scene::ShadowComponent& Shadow() noexcept { return *FindComponent<NS::Scene::ShadowComponent>(); }
};
