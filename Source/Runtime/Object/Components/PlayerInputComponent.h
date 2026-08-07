#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Object
{
    class CharacterMovementComponent;

    /// @brief Keyboard / Gamepad の入力を CharacterMovementComponent に橋渡しする Component
    /// @details WASD + 左スティックを camera forward 相対の world 方向に変換し、Space / Gamepad A の
    /// 押した瞬間と押しっぱなしを movement の SetJumpPressed / SetJumpHeld へ流す
    /// 基準の forward は毎ステップ scene の CameraBrain から自分で読む
    /// 入力はプロセス全体で 1 個の Input::Get() を直接読む
    class PlayerInputComponent : public Component
    {
    public:
        PlayerInputComponent() noexcept;

        /// camera 相対移動用の水平 forward を注入し、 XZ 平面で Y=0 とする。未注入時は world +Z
        /// Brain の居る scene では OnUpdate が毎ステップ上書きする。 Brain 不在 (テスト等) では直接設定に使う
        void SetCameraForward(const NS::Core::Vector3& cameraForwardHorizontal) noexcept;

        [[nodiscard]] CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        /// 同じ object の movement をここで解決する。見つからなければ OnUpdate は何もしない
        void OnStart() override;
        void OnUpdate() override;

        // 入力の橋渡しだけで保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(PlayerInputComponent, Component)

    private:
        CharacterMovementComponent* m_movement = nullptr;    // 橋渡し先の移動 Component (非所有)
        NS::Core::Vector3 m_cameraForward{0.0f, 0.0f, 1.0f}; // camera 相対移動用の水平 forward
    };
} // namespace NS::Object
