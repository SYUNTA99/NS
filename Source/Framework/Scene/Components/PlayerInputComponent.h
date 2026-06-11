#pragma once

/// @file PlayerInputComponent.h
/// @brief Keyboard / Gamepad の入力を CharacterMovementComponent に橋渡しする Component
///
/// WASD + Left Stick を camera forward 相対の world direction に変換し、Space / Gamepad A の
/// rising edge / held を movement の SetJumpPressed / SetJumpHeld へ流す。Camera への直接依存は
/// 持たず、`SetCameraForward()` で LevelEditorScene が毎フレーム値を注入する

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Platform
{
    class Input;
}

namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::Scene
{
    class CharacterMovementComponent;

    class PlayerInputComponent : public Component
    {
    public:
        /// movement を受け取って構築する
        explicit PlayerInputComponent(CharacterMovementComponent* movement) noexcept;

        /// camera 相対移動用の水平 forward (XZ, Y=0) を注入。未注入時は world +Z
        void SetCameraForward(const NS::Math::Vector3& cameraForwardHorizontal) noexcept;

        /// 入力ソースを注入。null で何もしない
        void SetInput(NS::Platform::Input* input) noexcept;

        /// ImGui 注入。WantCaptureKeyboard==true 中はキーボード入力を無視 (gamepad は維持)。null でガード無効
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept;

        [[nodiscard]] CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        void OnUpdate() override;

    private:
        CharacterMovementComponent* m_movement = nullptr;
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;
        NS::Math::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f};
    };
} // namespace NS::Scene
