#pragma once

#include "Runtime/Core/Coroutine.h"
#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    class ScreenFade;

    //! @brief ゴール接触に応えてクリアのシーケンスを流す。プレイヤーに載せる
    //! @details 判定が出そろった後、同じ LateUpdate 内でフラグを見て演出シーケンスを始める: 入力を切る → 暗転 →
    //! 全黒の裏でやり直す → 明転 → 入力を返す。世界は止めず入力だけを切る
    //! シーケンスの駆動も自分の OnUpdate が持つ。編集へ戻る破棄はエディタが Cancel を呼ぶ
    class Finisher : public NS::Obj::Component
    {
    public:
        Finisher() noexcept;

        void OnUpdate() override;

        //! 進行中のシーケンスを途中のまま破棄する。残すと次のプレイ開始で前回の演出が突然発火する
        void Cancel() noexcept { m_sequences.CancelAll(); }

        //! クリアのシーケンスが進行中か
        [[nodiscard]] bool IsRunning() const noexcept { return m_sequences.IsRunning(); }

        // ゴールの達成感を一拍味わわせてから、もう一周へ送り出すテンポ。短いと唐突で長いと待たされる
        NS_REFLECT_BEGIN(Finisher, NS::Obj::Component)
        NS_REFLECT_FIELD(m_fadeOutSeconds, "暗転秒")
        NS_REFLECT_FIELD(m_fadeInSeconds, "明転秒")
        NS_REFLECT_END()

    private:
        //! 同じ owner に載った暗転 component。シーケンスの待ちをまたぐので毎回引き直す
        [[nodiscard]] ScreenFade* Fade() noexcept;

        //! クリアのシーケンス: 暗転 → 全黒の裏で走行をやり直す → 明転
        [[nodiscard]] NS::Core::Coroutine ClearSequence();

        //! プレイヤー入力の active を切り替える。シーケンスの間だけ入力を切るのに使う
        void SetPlayerInputActive(bool active) noexcept;

        float m_fadeOutSeconds = 0.4f;         // ゴールから全黒になるまでの秒。0 以下は即座に黒
        float m_fadeInSeconds = 0.4f;          // やり直した後に明けるまでの秒。0 以下は即座に明ける
        NS::Core::CoroutineRunner m_sequences; // 演出シーケンス
    };
} // namespace NS::Game::Level
