#pragma once

/// @file ThirdPersonFollowComponent.h
/// @brief Mario 系ジャンプアクションの追従カメラ
///        critically-damped spring で distance を smoothing、マウス/右スティック手動回転、
///        sensitivity/invert と自動ズーム距離 (idle/run/jump) を member 保持し setter で調整可
///        FOV は CameraComponent 側が所有する

#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"

namespace NS::Platform
{
    class Input;
}

namespace NS::Scene
{
    class CharacterMovementComponent;
    class Transform;
    class GameObject;

    /// 実カメラは持たず、追従姿勢を pose として返す follow 仮想カメラ。CameraBrain が実カメラへ書く
    class ThirdPersonFollowComponent : public VirtualCameraComponent
    {
    public:
        /// target を受け取って構築する
        explicit ThirdPersonFollowComponent(Transform* target) noexcept;

        void SetTarget(Transform* target) noexcept;
        [[nodiscard]] Transform* Target() const noexcept { return m_target; }

        /// 右スティック / マウス回転の入力ソース。null では旋回 0
        void SetInput(NS::Platform::Input* input) noexcept;

        /// Dynamic zoom 判定用 (grounded / horizontal velocity)。null で idle 距離固定
        void SetMovement(const CharacterMovementComponent* movement) noexcept;

        /// 設定 (将来 Settings UI から bridge)
        void SetSensX(float radPerPixel) noexcept;
        [[nodiscard]] float SensX() const noexcept { return m_sensX; }
        void SetSensY(float radPerPixel) noexcept;
        [[nodiscard]] float SensY() const noexcept { return m_sensY; }
        void SetInvertX(bool invert) noexcept;
        [[nodiscard]] bool IsInvertX() const noexcept { return m_invertX; }
        void SetInvertY(bool invert) noexcept;
        [[nodiscard]] bool IsInvertY() const noexcept { return m_invertY; }

        /// 自動ズームの距離 3 段 (idle / run / jump)。非正値は無視する
        void SetAutoDistances(float idle, float run, float jump) noexcept;
        /// 自動ズームで run 距離へ切替える水平速度しきい値
        void SetRunSpeedThreshold(float speed) noexcept;

        /// 距離を手動固定。dynamic zoom を無効化し、ClearManualDistance() で再有効化する
        void SetDistance(float distance) noexcept;
        void ClearManualDistance() noexcept;
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] bool IsManualDistance() const noexcept { return m_manualDistance; }

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

        /// fixed step で yaw/pitch・distance spring を更新。最終姿勢は EvaluatePose が返す (ガタつき回避)
        void OnUpdate() override;

        /// 補間 target (alpha) を追う最終姿勢を返す。Brain が選択時に実カメラへ書く (旧 ApplyCameraTransform)
        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

    private:
        Transform* m_target = nullptr;
        NS::Platform::Input* m_input = nullptr;
        const CharacterMovementComponent* m_movement = nullptr;

        float m_yaw = 0.0f;
        float m_pitch = -0.2618f;

        float m_distance = 6.0f;
        float m_desiredDistance = 6.0f;
        float m_springOmega = 6.0f;
        bool m_manualDistance = false;

        float m_idleDistance = 5.0f;
        float m_runDistance = 6.0f;
        float m_jumpDistance = 7.0f;
        float m_runSpeedThreshold = 4.0f;

        float m_headHeight = 1.2f;

        float m_sensX = 0.0030f;
        float m_sensY = 0.0030f;
        float m_stickSensX = 2.0f;
        float m_stickSensY = 1.5f;
        bool m_invertX = false;
        bool m_invertY = false;

        float m_pitchMin = -1.396f;
        float m_pitchMax = -0.0873f;
    };
} // namespace NS::Scene
