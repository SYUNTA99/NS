#pragma once

/// @file HazardComponent.h
/// @brief 接触ダメージ Component。
///
/// @details OnPlayerOverlap で `PlayState::playerHealth` を 1 ずつ減算する。 LevelData は
/// 一切変更しない (CRC32 round-trip 互換)。 health が 0 に到達した
/// 瞬間に `PlayState::deathTriggered` を立てる。 Game.cpp 側がそのフラグを観測して
/// `NS::App::Application::Quit()` を呼ぶ placeholder 死亡パスとなる。 HUD / 回復 / 死亡演出 /
/// respawn は将来本 Component を非侵襲に拡張する想定。

#include "Framework/Scene/Component.h"

namespace NS::Game::Level
{
    struct PlayState;
}

namespace NS::Scene
{
    /// 触れたプレイヤーに毎フレーム 1 ダメージを与える trigger Component。
    /// 自身は world に対して描画も衝突応答も持たない (HazardBlock 側の StaticColliderComponent が
    /// 衝突応答を、 MeshComponent が描画を別途担う)。
    class HazardComponent : public Component
    {
    public:
        explicit HazardComponent(GameObject* owner) noexcept;

        /// LevelEditorScene / PlayMode が capsule × hazard AABB の overlap を検出した frame で呼ぶ。
        /// `playState.playerHealth` を 1 減算 (下限 0 でクランプ)、 0 到達で `deathTriggered=true`。
        void OnPlayerOverlap(NS::Game::Level::PlayState& playState) noexcept;
    };
} // namespace NS::Scene
