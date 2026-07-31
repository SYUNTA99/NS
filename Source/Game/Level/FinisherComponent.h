#pragma once

#include "Runtime/Core/Coroutine.h"
#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    class ScreenFadeComponent;

    /// @brief ゴール接触に応えてクリアの台本を流す。プレイヤーに載せる
    /// @details LateUpdate の判定が出そろった後、同じ tick 内で旗を見て台本を始める: 入力を切る → 暗転 →
    /// 全黒の裏でやり直す → 明転 → 入力を返す。世界は止めず操作だけを奪う
    /// 台本の駆動も自分の OnUpdate が持つ。編集へ戻る破棄はエディタが Cancel を呼ぶ
    class FinisherComponent : public NS::Object::Component
    {
    public:
        /// ゴールの達成感を一拍味わわせてから、もう一周へ送り出すためのテンポ
        /// 短いと唐突で長いと待たされるので前後 0.4 秒にした
        static constexpr float k_FadeOutSeconds = 0.4f;
        static constexpr float k_FadeInSeconds = 0.4f;

        FinisherComponent() noexcept;

        void OnUpdate() override;

        /// 進行中の台本を途中のまま破棄する。残すと次のプレイ開始で前回の演出が突然発火する
        void Cancel() noexcept { m_sequences.CancelAll(); }

        /// クリアの台本が進行中か
        [[nodiscard]] bool IsRunning() const noexcept { return m_sequences.IsRunning(); }

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(FinisherComponent, NS::Object::Component)

    private:
        /// 同じ持ち主に載った暗転の兄弟。台本の待ちをまたぐので毎回引き直す
        [[nodiscard]] ScreenFadeComponent* Fade() noexcept;

        /// クリアの台本: 暗転 → 全黒の裏でレベルを組み直す → 明転
        [[nodiscard]] NS::Core::Coroutine ClearSequence();

        /// プレイヤー入力の札を切り替える。台本の間だけ操作を奪うのに使う
        void SetPlayerInputActive(bool active) noexcept;

        NS::Core::CoroutineRunner m_sequences; // 演出台本の駆動役
    };
} // namespace NS::Game::Level
