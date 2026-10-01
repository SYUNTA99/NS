#pragma once

#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/Components/OverlayRenderer.h"
#include "Runtime/Platform/Gamepad.h"

namespace NS::Obj
{
    //! @brief パッドの振動 1 回。始めの値から直線に減らし、書くフレーム数で切る
    struct HitPadVibration
    {
        NS::Platform::GamepadVibration start{}; //!< 始めの速さ
        int fadeFrames = 0;                     //!< 始めの値から 0 まで減るフレーム数
        int frames = 0;                         //!< 書くフレーム数。fadeFrames 以下。0 なら震わせない
    };

    //! @brief 当たりの演出 1 回。白の光・カメラの揺れ・寄りと傾き・パッドの振動
    struct HitReactionDesc
    {
        int flashFrames = 0;           //!< 白の光のフレーム数。0 なら光らない
        float flashAlpha = 0.0f;       //!< 白の光の始めの濃さ。残りのフレーム数に比例して薄くなる
        CameraShakeDesc shake{};       //!< カメラの揺れ。フレーム数 0 なら揺らさない
        CameraZoomRollDesc zoomRoll{}; //!< 寄りと傾き。倍率 1 の設定も積み、前の当たりの寄りを残さない
        HitPadVibration pad{};         //!< パッドの振動
    };

    //! @brief 当たりの演出を出す部品。オデッセイの HitReactionKeeper に当たる
    //! @details 当たりを決めた側が HitReactionDesc を組んで Play を呼ぶ。演出の中身 (白・揺れ・寄り・振動) はここが出す
    //! 揺れと寄りはカメラの窓口から管理役へモディファイアを積む。白は画面へ重ね描き、振動はパッドへ毎フレーム書く
    //! Play を呼んだフレームに最初の姿を出し、次の更新から進める
    //! 当たりのタイムライン (中心・外れの 2 本) を作る時は、演出の並びをここへ持たせる
    class HitReaction : public OverlayRenderer
    {
    public:
        HitReaction() noexcept;
        [[nodiscard]] int OverlayOrder() const noexcept override { return 1; }

        //! 演出を始める。前の演出が残っていても、渡した設定で始め直す
        void Play(const HitReactionDesc& desc);

        //! 演出を全て止める。白を消し、振動を 0 にし、積んだカメラの効果を外す
        void Stop();

        //! 白の残りフレーム数。出していない場合 0
        [[nodiscard]] int FlashFramesRemaining() const noexcept { return m_flashRemaining; }

        //! 白を 1 フレーム薄め、振動を書く
        void OnUpdate() override;

        //! 残りのフレーム数に比例して薄くなる白を画面全体へ重ねる
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

        //! 途中で外れても振動とカメラの効果を残さない
        void OnKill() noexcept override { OnEndPlay(); }
        void OnEndPlay() override;

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(HitReaction, OverlayRenderer)

    private:
        // 振動を始めてからのフレーム数に応じた速さをパッドへ書く。書くフレーム数に届いたフレームは 0 を書いて止める
        void WritePadVibration();

        int m_flashRemaining = 0;  // 白の残りフレーム数
        int m_flashFrames = 0;     // 白の始めのフレーム数。薄める割合の分母
        float m_flashAlpha = 0.0f; // 白の始めの濃さ
        HitPadVibration m_pad{};   // 書いている振動
        int m_padElapsed = 0;      // 振動を始めたフレームから数えたフレーム数
        bool m_padRunning = false; // 振動を書いている最中か
        bool m_justPlayed = false; // Play の後まだ更新を通っていないか
    };
} // namespace NS::Obj
