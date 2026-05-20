#pragma once

/// @file third_person_follow_component.h
/// @brief Mario 系ジャンプアクションの追従カメラ (〜C5)。
///        critically-damped spring で distance を smoothing、 マウス/右スティック手動回転、
///         FOV/sensitivity/invert を member 保持、Dynamic zoom (idle 5 / run 6 / jump 7) を
///        movement の grounded / horizontal velocity から自動切替。

#include "ns/core/math.h"
#include "ns/scene/component.h"

namespace ns::platform
{
    class Input;
}

namespace ns::scene
{
    class CameraComponent;
    class CharacterMovementComponent;
    class Transform;

    class ThirdPersonFollowComponent : public Component
    {
    public:
        explicit ThirdPersonFollowComponent(Transform* target) noexcept;

        void SetTarget(Transform* target) noexcept;
        [[nodiscard]] Transform* Target() const noexcept { return m_target; }

        /// 出力先 Camera を注入。null では OnUpdate は no-op。
        void SetCamera(CameraComponent* camera) noexcept;

        ///  右スティック / マウス回転の入力ソース。null では旋回 0。
        void SetInput(ns::platform::Input* input) noexcept;

        /// Dynamic zoom 判定用 (grounded / horizontal velocity)。null で idle 距離固定。
        void SetMovement(const CharacterMovementComponent* movement) noexcept;

        ///  設定 ( で Settings UI から bridge)。
        void SetFovY(float radians) noexcept;
        [[nodiscard]] float FovY() const noexcept { return m_fovY; }
        void SetSensX(float radPerPixel) noexcept;
        [[nodiscard]] float SensX() const noexcept { return m_sensX; }
        void SetSensY(float radPerPixel) noexcept;
        [[nodiscard]] float SensY() const noexcept { return m_sensY; }
        void SetInvertX(bool invert) noexcept;
        [[nodiscard]] bool IsInvertX() const noexcept { return m_invertX; }
        void SetInvertY(bool invert) noexcept;
        [[nodiscard]] bool IsInvertY() const noexcept { return m_invertY; }

        /// 距離の手動オーバーライド (test / cinematic 用)。default は dynamic zoom。
        void SetDistance(float distance) noexcept;
        [[nodiscard]] float Distance() const noexcept { return m_distance; }

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

        void OnUpdate(float dt) override;

    private:
        Transform* m_target = nullptr;
        CameraComponent* m_camera = nullptr;
        ns::platform::Input* m_input = nullptr;
        const CharacterMovementComponent* m_movement = nullptr;

        float m_yaw = 0.0f;
        float m_pitch = -0.2618f;

        float m_distance = 6.0f;
        float m_desiredDistance = 6.0f;
        float m_springOmega = 6.0f;

        float m_headHeight = 1.2f;

        float m_fovY = 1.0472f;
        float m_sensX = 0.0030f;
        float m_sensY = 0.0030f;
        float m_stickSensX = 2.0f;
        float m_stickSensY = 1.5f;
        bool m_invertX = false;
        bool m_invertY = false;

        float m_pitchMin = -1.396f;
        float m_pitchMax = -0.0873f;
    };
} // namespace ns::scene
