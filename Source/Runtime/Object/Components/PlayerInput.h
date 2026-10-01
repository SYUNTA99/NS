#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Obj
{
    //! @brief Keyboard / Gamepad の入力を読んで値として持つ Component
    //! @details WASD + 左スティックを camera forward 相対の world 方向に変換し、Space / Gamepad A の
    //! 押した瞬間と押しっぱなし、マウス右ボタン / 左トリガーの手放しを合わせた 7 つの値を持つ。渡し先は知らない
    //! 基準の forward は毎ステップ scene の CameraManager から自分で読む
    //! 入力はプロセス全体で 1 個の Input::Get() を直接読む
    class PlayerInput : public Component
    {
    public:
        PlayerInput() noexcept;

        //! camera 相対移動用の水平 forward を注入し、XZ 平面で Y=0 とする。未注入時は world +Z
        //! CameraManager の居る scene では OnUpdate が毎ステップ上書きする。CameraManager 不在 (テスト等)
        //! では直接設定に使う
        void SetCameraForward(const NS::Core::Vector3& cameraForwardHorizontal) noexcept;

        //! @brief 目標の移動方向と速度スケールを直接書く
        //! @param[in] worldDir world 空間の目標移動方向。そのまま控える
        //! @param[in] speedScale01 目標速度スケール。0..1 に丸める
        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
        //! @brief 掴まりで使うローカル入力を直接書く。どちらも -1..1 に丸める
        void SetClimbMove(float localRight, float localForward) noexcept;
        //! 跳びの押した瞬間を立てる。ConsumePressed が下ろす
        void SetJumpPressed() noexcept { m_jumpPressed = true; }
        //! 掴まりの手放しの押した瞬間を立てる。ConsumePressed が下ろす
        void SetReleaseLedgePressed() noexcept { m_releaseLedgePressed = true; }
        //! 跳びの押しっぱなしを書く
        void SetJumpHeld(bool held) noexcept { m_jumpHeld = held; }
        //! 跳びと掴まりの手放しの押した瞬間を下ろす
        void ConsumePressed() noexcept;
        //! 移動・掴まり・跳びの入力を全て 0 と false に戻す
        void ResetMovementInput() noexcept;

        // 読み取った値は自分で持つ。渡し先の型を include できない場所からも引ける
        //! 直近のフレームで作った world 空間の目標移動方向。長さは入力の強さのまま
        [[nodiscard]] NS::Core::Vector3 DesiredDirection() const noexcept { return m_desiredDir; }
        //! 直近のフレームの目標速度スケール。入力の大きさで 0..1
        [[nodiscard]] float DesiredSpeedScale() const noexcept { return m_desiredSpeedScale; }
        //! 掴まりで使う、camera 回転をかける前のローカル入力
        [[nodiscard]] float ClimbRight() const noexcept { return m_climbRight; }
        [[nodiscard]] float ClimbForward() const noexcept { return m_climbForward; }
        [[nodiscard]] bool JumpPressed() const noexcept { return m_jumpPressed; }
        [[nodiscard]] bool JumpHeld() const noexcept { return m_jumpHeld; }
        [[nodiscard]] bool ReleaseLedgePressed() const noexcept { return m_releaseLedgePressed; }

        void OnUpdate() override;

        // 入力を渡すだけで保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(PlayerInput, Component)

    private:
        NS::Core::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f}; // camera 相対移動用の水平 forward

        // 直近に稼働したフレームで読み取った値。稼働していない間は書き換わらない
        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // world 空間の目標移動方向
        float m_desiredSpeedScale = 0.0f;
        float m_climbRight = 0.0f;
        float m_climbForward = 0.0f;
        bool m_jumpPressed = false;
        bool m_jumpHeld = false;
        bool m_releaseLedgePressed = false;
        bool m_prevReleaseLedgeHeld = false;
    };
} // namespace NS::Obj
