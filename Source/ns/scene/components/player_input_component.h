#pragma once

/// @file player_input_component.h
/// @brief Keyboard / Gamepad の入力を CharacterMovementComponent に橋渡しする Component。
///
/// WASD + Left Stick を camera forward 相対の world direction に変換し、Space / Gamepad A の
/// rising edge / held を movement の SetJumpPressed / SetJumpHeld へ流す。Camera への直接依存は
/// 持たず、`SetCameraForward()` で MainScene が毎フレーム値を注入する ( と independent)。

#include "ns/core/math.h"
#include "ns/scene/component.h"

namespace ns::platform
{
    class Input;
}

namespace ns::scene
{
    class CharacterMovementComponent;

    class PlayerInputComponent : public Component
    {
    public:
        explicit PlayerInputComponent(CharacterMovementComponent* movement) noexcept;

        /// MainScene が active CameraComponent から計算した水平 forward (XZ 平面、Y は 0) を注入する。
        /// 注入前の default は world -Z+ 方向。
        void SetCameraForward(const ns::core::Vector3& cameraForwardHorizontal) noexcept;

        /// 入力ソースを注入。MainScene が `Application::Get()->Input()` を渡す。
        /// null で no-op。
        void SetInput(ns::platform::Input* input) noexcept;

        [[nodiscard]] CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        void OnUpdate(float dt) override;

    private:
        CharacterMovementComponent* m_movement = nullptr;
        ns::platform::Input* m_input = nullptr;
        ns::core::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f};
    };
} // namespace ns::scene
