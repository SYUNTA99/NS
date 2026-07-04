#pragma once

/// @file PlayerInputComponent.h
/// @brief Keyboard / Gamepad の入力を CharacterMovementComponent に橋渡しする Component
///
/// WASD + Left Stick を camera forward 相対の world direction に変換し、Space / Gamepad A の
/// rising edge / held を movement の SetJumpPressed / SetJumpHeld へ流す。Camera への直接依存は
/// 持たず、`SetCameraForward()` で LevelPlayScene が毎フレーム値を注入する
/// 入力ソースは process 全体で 1 個の `Input::Get()` を直接読む

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    class CharacterMovementComponent;

    class PlayerInputComponent : public Component
    {
    public:
        PlayerInputComponent() noexcept;

        /// camera 相対移動用の水平 forward を注入し、 XZ 平面で Y=0 とする。未注入時は world +Z
        void SetCameraForward(const NS::Math::Vector3& cameraForwardHorizontal) noexcept;

        [[nodiscard]] CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        /// 兄弟の movement をここで解決する。見つからなければ OnUpdate は何もしない
        void OnStart() override;
        void OnUpdate() override;

        // 入力の橋渡しだけで保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(PlayerInputComponent, Component)

    private:
        CharacterMovementComponent* m_movement = nullptr;
        NS::Math::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f};
    };
} // namespace NS::Scene
