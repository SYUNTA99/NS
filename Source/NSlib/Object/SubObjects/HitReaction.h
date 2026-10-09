#pragma once

#include "NSlib/Object/Scene/HitScreenDirector.h"
#include "NSlib/Object/Scene/PadRumbleDirector.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/CameraModifier.h"

namespace NS::Obj
{
    //! @brief 当たりの演出を頼む部品
    //! @details 並びと始めるフレームは当たりのタイムラインが決める。揺れと寄りは CameraManager、
    //! 白・震えの線・歪みの輪は HitScreenDirector、振動は PadRumbleDirector が進める
    class HitReaction : public SubObject
    {
    public:
        HitReaction() noexcept = default;

        //! シーンに 1 つの HitScreenDirector と PadRumbleDirector
        //! を作る。更新の最中に作ると、そのフレームの段に間に合わない
        void OnStart() override;

        //! @brief 白の光を頼む。前の白が残っていても始め直す
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void StartFlash(int frames, float alpha) noexcept;

        //! @brief カメラの揺れを始める
        //! @param[in] desc 揺れの設定。フレーム数 0 以下なら揺らさない
        //! @return 揺れを積んだか、フレーム数 0 以下で揺らさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartShake(const CameraShakeDesc& desc);

        //! @brief カメラの沈む揺れを始める
        //! @param[in] desc 4 拍の形。フレーム数 0 以下なら揺らさない
        //! @return 揺れを積んだか、フレーム数 0 以下で揺らさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartSink(const CameraSinkDesc& desc);

        //! @brief カメラのずれを始める。位置と注視点を同じだけずらし、向きは変えない
        //! @param[in] desc ずらす向きと距離の曲線。フレーム数 0 以下ならずらさない
        //! @return ずれを積んだか、フレーム数 0 以下でずらさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartNudge(const CameraNudgeDesc& desc);

        //! @brief カメラのトラウマを足す。揺れの途中なら前のトラウマに足す
        //! @param[in] desc 足す量と揺れの形
        //! @return 足した場合 true。量が壊れている・カメラの管理役が無い場合 false
        bool AddTrauma(const CameraTraumaDesc& desc);

        //! @brief カメラの寄りと傾きを始める
        //! @param[in] desc 寄りと傾きの設定
        //! @return 積んだ場合 true。設定が壊れている・カメラの管理役が無い場合 false
        bool StartZoomRoll(const CameraZoomRollDesc& desc);

        //! @brief 震えの線を頼む。前の線が残っていても始め直す
        //! @param[in] desc 震えの線の設定。半径かフレーム数が 0 以下なら出さない
        void StartShakeLines(const HitShakeLinesDesc& desc) noexcept;

        //! @brief 歪みの輪を頼む。前の輪が残っていても始め直す
        //! @param[in] desc 歪みの輪の設定。フレーム数が 0 以下なら出さない
        void StartDistortionRing(const HitDistortionRingDesc& desc) noexcept;

        //! @brief この部品が頼んだ歪みの輪を消す
        //! @details Stop は輪を消さない
        void StopDistortionRing() noexcept;

        //! @brief パッドの振動を頼む。前の振動が残っていても始め直す
        //! @param[in] pad 振動の設定
        void StartPadVibration(const HitPadVibration& pad);
        //! @brief 書いている振動に pad を重ねる
        //! @details 重ねたフレームを 0 として pad を進め、値を足す。pad の長さを過ぎたら 0 を足す。
        //! 何も書いていなければ StartPadVibration と同じ
        //! @param[in] pad 重ねる振動
        void BlendPadVibration(const HitPadVibration& pad);

        //! @brief この部品が頼んだ演出を止める。白と震えの線を消し、振動を 0 にし、揺れ・ずれ・寄りを外す
        //! @details トラウマは残す。次の当たりが止めてから始め直しても、続けて当てたトラウマは足される
        void Stop();

        //! 途中で外れても振動とカメラの効果を残さない
        void OnKill() noexcept override { OnEndPlay(); }
        void OnEndPlay() override;

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(HitReaction, SubObject)
    };
} // namespace NS::Obj
