#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/SubObject.h"

namespace NS
{
    //! 機器の入力をカメラ相対の移動方向と操作状態へ変換する部品
    //! 操作ロック中は中立値を保持する
    class PlayerInput : public SubObject
    {
    public:
        PlayerInput() noexcept = default;

        //! 水平の基準方向。シーンにカメラがあれば更新時に上書きし、未指定なら +Z
        void SetCameraForward(const Vector3& cameraForwardHorizontal) noexcept;

        //! @brief 目標の移動方向と速度スケールを直接書く
        //! @param[in] worldDir 世界座標系の目標移動方向。そのまま控える
        //! @param[in] speedScale01 目標速度スケール。0..1 に丸める
        void SetDesiredMove(const Vector3& worldDir, float speedScale01) noexcept;
        //! @brief 掴まりで使うローカル入力を直接書く。どちらも -1..1 に丸める
        void SetClimbMove(float localRight, float localForward) noexcept;
        //! 跳びの押した瞬間を立てる。ConsumePressed が下ろす
        void SetJumpPressed() noexcept { m_jumpPressed = true; }
        //! 掴まりの手放しの押した瞬間を立てる。ConsumePressed が下ろす
        void SetReleaseLedgePressed() noexcept { m_releaseLedgePressed = true; }
        void SetJumpHeld(bool held) noexcept { m_jumpHeld = held; }
        //! 跳びと掴まりの手放しの押した瞬間と、突進の押しの控えと、出し直しの押しを下ろす
        void ConsumePressed() noexcept;
        //! 移動・掴まり・跳びの入力を全て 0 と false に戻す
        void ResetMovementInput() noexcept;
        //! 体当たりのボタンの押しを書く。試しが機器を通さずに押しを入れる時に使い、操作の停止は見ない
        void SetSlamHeld(bool held) noexcept { m_slamHeld = held; }
        //! 入力を中立へ戻し、解除まで機器からの操作を無視する。直接値を設定する関数には影響しない
        void SetLocked(bool locked) noexcept;
        [[nodiscard]] bool IsLocked() const noexcept { return m_locked; }

        // 読み取った値は自分で持つ。渡し先の型を include できない場所からも引ける
        //! 直近のフレームの世界座標系での目標移動方向。長さは入力の強さのまま
        [[nodiscard]] Vector3 DesiredDirection() const noexcept { return m_desiredDir; }
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
        //! 置物の出し直しのボタン (R かゲームパッドの Back) を押した場合 true、それ以外の場合は false
        [[nodiscard]] bool RespawnPressed() const noexcept { return m_respawnPressed; }

        //! キーボードとゲームパッドから今の入力を読む。有効でない間は何もしない
        void Update();

        // 入力を渡すだけで保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(PlayerInput, SubObject)

    private:
        Vector3 m_cameraForward{0.0f, 0.0f, 1.0f}; // camera 相対移動用の水平 forward

        // 直近に稼働したフレームで読み取った値。稼働していない間は書き換わらず、止めの間は中立を書く
        Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // 世界座標系の目標移動方向
        float m_desiredSpeedScale = 0.0f;
        float m_climbRight = 0.0f;
        float m_climbForward = 0.0f;
        bool m_jumpPressed = false;
        bool m_jumpHeld = false;
        bool m_releaseLedgePressed = false;
        bool m_prevReleaseLedgeHeld = false;
        bool m_slamHeld = false;
        bool m_slamSeen = false; // ConsumePressed の後に突進を押したか
        bool m_respawnPressed = false;
        bool m_locked = false; // 操作の停止中か。保存しない
    };
} // namespace NS
