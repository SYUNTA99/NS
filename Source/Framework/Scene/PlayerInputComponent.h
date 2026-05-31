#pragma once

/// @file PlayerInputComponent.h
/// @brief Keyboard / Gamepad の入力を CharacterMovementComponent に橋渡しする Component。
///
/// WASD + Left Stick を camera forward 相対の world direction に変換し、Space / Gamepad A の
/// rising edge / held を movement の SetJumpPressed / SetJumpHeld へ流す。Camera への直接依存は
/// 持たず、`SetCameraForward()` で LevelEditorScene が毎フレーム値を注入する。

#include "Framework/Core/Math.h"
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
        /// GameObject owner と movement を同時に受け取って auto-register する ctor。
        PlayerInputComponent(NS::Scene::GameObject* owner, CharacterMovementComponent* movement) noexcept;

        /// LevelEditorScene が active CameraComponent から計算した水平 forward (XZ 平面、Y は 0) を注入する。
        /// 注入前の default は world -Z+ 方向。
        void SetCameraForward(const NS::Core::Vector3& cameraForwardHorizontal) noexcept;

        /// 入力ソースを注入。LevelEditorScene が `Application::Get()->Input()` を渡す。
        /// null で no-op。
        void SetInput(NS::Platform::Input* input) noexcept;

        /// ImGui コンテキストを注入。 `WantCaptureKeyboard()` が true の間 (テキスト入力中など) は
        /// キーボード由来の移動 / ジャンプを無視する (gamepad は維持)。 null でガード無効。
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept;

        [[nodiscard]] CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        void OnUpdate() override;

    private:
        CharacterMovementComponent* m_movement = nullptr;
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;
        NS::Core::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f};
    };
} // namespace NS::Scene
