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

    //! @brief 当たりの演出を出す部品。オデッセイの HitReactionKeeper に当たる
    //! @details 当たりを決めた側が、白・揺れ・寄り・振動を別々に始める。並びと始めるフレームは当たりのタイムラインが決め、
    //! ここは始めた物を出して進めるだけ。揺れと寄りはカメラの窓口から管理役へモディファイアを積む。
    //! 白は画面へ重ね描き、振動はパッドへ毎フレーム書く。白と振動は始めたフレームに最初の姿を出し、次の更新から進める
    class HitReaction : public OverlayRenderer
    {
    public:
        HitReaction() noexcept;
        [[nodiscard]] int OverlayOrder() const noexcept override { return 1; }

        //! @brief 白の光を始める。前の白が残っていても始め直す
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void StartFlash(int frames, float alpha) noexcept;

        //! @brief カメラの揺れを始める
        //! @param[in] desc 揺れの設定。フレーム数 0 以下なら揺らさない
        //! @return 揺れを積んだか、フレーム数 0 以下で揺らさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartShake(const CameraShakeDesc& desc);

        //! @brief カメラの寄りと傾きを始める
        //! @param[in] desc 寄りと傾きの設定
        //! @return 積んだ場合 true。設定が壊れている・カメラの管理役が無い場合 false
        bool StartZoomRoll(const CameraZoomRollDesc& desc);

        //! @brief パッドの振動を始める。前の振動が残っていても始め直す
        //! @param[in] pad 振動の設定
        void StartPadVibration(const HitPadVibration& pad);

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
        bool m_padRunning = false;       // 振動を書いている最中か
        bool m_flashJustStarted = false; // 白を始めた後まだ更新を通っていないか
        bool m_padJustStarted = false;   // 振動を始めた後まだ更新を通っていないか
    };
} // namespace NS::Obj
