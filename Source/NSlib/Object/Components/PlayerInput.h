#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Component.h"

namespace NS::Obj
{
    //! @brief Keyboard / Gamepad / マウスのボタンの入力を読んで値として持つ Component
    //! @details WASD + 左スティックを camera forward 相対の world 方向に変換し、Space / Gamepad A の
    //! 押した瞬間と押しっぱなし、マウス右ボタン / 左トリガーの手放し、
    //! マウス左 / Gamepad X の体当たりの押しを合わせた 8 つの値を持つ。渡し先は知らない
    //! 操作の停止 (SetLocked) の間は機器を読んでも全ての値を中立に書く
    //! 基準の forward は毎ステップ scene の CameraManager から自分で読む
    //! 入力はプロセス全体で 1 個の Input::Get() を直接読む
    class PlayerInput : public Component
    {
    public:
        PlayerInput() noexcept = default;

        //! camera 相対移動用の水平 forward を注入し、XZ 平面で Y=0 とする。未注入時は world +Z
        //! CameraManager の居る scene では OnUpdate が毎ステップ上書きする。CameraManager 不在 (テスト等)
        //! では直接設定に使う
        void SetCameraForward(const NS::Vector3& cameraForwardHorizontal) noexcept;

        //! @brief 目標の移動方向と速度スケールを直接書く
        //! @param[in] worldDir world 空間の目標移動方向。そのまま控える
        //! @param[in] speedScale01 目標速度スケール。0..1 に丸める
        void SetDesiredMove(const NS::Vector3& worldDir, float speedScale01) noexcept;
        //! @brief 掴まりで使うローカル入力を直接書く。どちらも -1..1 に丸める
        void SetClimbMove(float localRight, float localForward) noexcept;
        //! 跳びの押した瞬間を立てる。ConsumePressed が下ろす
        void SetJumpPressed() noexcept { m_jumpPressed = true; }
        //! 掴まりの手放しの押した瞬間を立てる。ConsumePressed が下ろす
        void SetReleaseLedgePressed() noexcept { m_releaseLedgePressed = true; }
        //! 跳びの押しっぱなしを書く
        void SetJumpHeld(bool held) noexcept { m_jumpHeld = held; }
        //! 跳びと掴まりの手放しの押した瞬間と、突進の押しの控えを下ろす
        void ConsumePressed() noexcept;
        //! 移動・掴まり・跳びの入力を全て 0 と false に戻す
        void ResetMovementInput() noexcept;
        //! 体当たりのボタンの押しを書く。試しが機器を通さずに押しを入れる時に使い、操作の停止は見ない
        void SetSlamHeld(bool held) noexcept { m_slamHeld = held; }
        //! @brief 操作を止めるか戻す
        //! @details 止める時はその場で移動・掴まり・跳び・体当たりの値を中立へ戻す。止めの間の OnUpdate は
        //! 機器を読んで手放しのトリガーの控えだけ進め、値は中立のまま書く。値を直接書く関数は止めを見ない
        //! @param[in] locked 止めるなら true、戻すなら false
        void SetLocked(bool locked) noexcept;
        //! 操作を止めている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsLocked() const noexcept { return m_locked; }

        // 読み取った値は自分で持つ。渡し先の型を include できない場所からも引ける
        //! 直近のフレームで作った world 空間の目標移動方向。長さは入力の強さのまま
        [[nodiscard]] NS::Vector3 DesiredDirection() const noexcept { return m_desiredDir; }
        //! 直近のフレームの目標速度スケール。入力の大きさで 0..1
        [[nodiscard]] float DesiredSpeedScale() const noexcept { return m_desiredSpeedScale; }
        //! 掴まりで使う、camera 回転をかける前のローカル入力
        [[nodiscard]] float ClimbRight() const noexcept { return m_climbRight; }
        [[nodiscard]] float ClimbForward() const noexcept { return m_climbForward; }
        [[nodiscard]] bool JumpPressed() const noexcept { return m_jumpPressed; }
        [[nodiscard]] bool JumpHeld() const noexcept { return m_jumpHeld; }
        [[nodiscard]] bool ReleaseLedgePressed() const noexcept { return m_releaseLedgePressed; }
        //! 体当たりのボタン (マウス左かゲームパッドの X) を押している場合 true、それ以外の場合は false
        [[nodiscard]] bool SlamHeld() const noexcept { return m_slamHeld; }

        void OnUpdate() override;

        // 入力を渡すだけで保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(PlayerInput, Component)

    private:
        NS::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f}; // camera 相対移動用の水平 forward

        // 直近に稼働したフレームで読み取った値。稼働していない間は書き換わらず、止めの間は中立を書く
        NS::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // world 空間の目標移動方向
        float m_desiredSpeedScale = 0.0f;
        float m_climbRight = 0.0f;
        float m_climbForward = 0.0f;
        bool m_jumpPressed = false;
        bool m_jumpHeld = false;
        bool m_releaseLedgePressed = false;
        bool m_prevReleaseLedgeHeld = false;
        bool m_slamHeld = false;
        bool m_slamSeen = false; // ConsumePressed の後に突進を押したか
        bool m_locked = false;   // 操作の停止中か。保存しない
    };
} // namespace NS::Obj
