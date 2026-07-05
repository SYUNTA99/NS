#pragma once

/// @file PlayFlowComponent.h
/// @brief プレイ進行の本体。 PlayState / PlayMode を所有し fixed step の進行を駆動する
///
/// @details 依存はコンストラクタで受け取らず、 所有 GameObject の scene を LevelPlayScene として
/// 解決して player / camera / level を読む。 編集モード中は SetActive(false) で寝かせて止める

#include "Framework/Scene/Component.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

class LevelPlayScene;

namespace NS::Game::Level
{
    class ClearFadeComponent;

    /// プレイ進行 Component。 paused / 暗転 / 死亡 / クリアの分岐と player 駆動を fixed step で回す
    class PlayFlowComponent : public NS::Scene::Component
    {
    public:
        PlayFlowComponent() noexcept = default;

        [[nodiscard]] PlayState& Play() noexcept { return m_play; }
        [[nodiscard]] PlayMode& PlayModeSub() noexcept { return m_playMode; }

        /// プレイ突入。 player をプレイヤー実体の位置へ置き、 物理 / 入力 / follow camera を有効化する
        void EnterPlay() noexcept;

        /// 編集モードへ。 paused / clear / death をリセットし player を凍結、 follow / area camera を休止する
        void ExitPlay() noexcept;

        /// レベルを頭から組み直す。 出現位置へ戻し health / coin / flag を全リセットして再開する
        void RestartLevel() noexcept;

        /// プレイ進行を dt だけ進める: 入力 → 物理 → ルール → area camera → 死亡 / リスポーン → カメラ追従
        /// 入力とカーソルは OnUpdate 側が扱うため、 Application 不在でも進められる
        void Tick(float dt);

        void OnStart() override;
        void OnUpdate() override;

        // 進行状態は保存しない。live の型検索が反射照合で引けるよう型名だけ登録する
        NS_REFLECT_NONE(PlayFlowComponent, NS::Scene::Component)

    private:
        /// 所有 scene を LevelPlayScene として返す。 未 attach なら nullptr。 初回参照で解決して控える
        [[nodiscard]] LevelPlayScene* OwnerScene() noexcept;

        /// 同じ進行役に載る暗転 Component を返す。 無ければ nullptr。 初回参照で解決して控える
        [[nodiscard]] ClearFadeComponent* FadeComp() noexcept;

        PlayState m_play{};
        PlayMode m_playMode{};

        // プレイ中のカーソル表示状態。 false は非表示の通常プレイ、 Esc で true の表示へ。 表示中の Esc で終了する
        bool m_playCursorShown = false;

        LevelPlayScene* m_scene = nullptr;
        ClearFadeComponent* m_fade = nullptr;
    };

} // namespace NS::Game::Level
