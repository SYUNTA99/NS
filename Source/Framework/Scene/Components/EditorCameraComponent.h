#pragma once

/// @file EditorCameraComponent.h
/// @brief NS::Scene::EditorCameraComponent — 編集モード用 free-fly カメラ Component
///
/// @details Spherical 座標 (yaw, pitch, distance) + center pivot 表現
/// Mouse 右ドラッグ = Orbit、 中ドラッグ = Pan、 Wheel = Zoom
/// Gamepad 右スティック = Orbit、 左スティック = Pan、 LT-RT = Zoom
/// UI (editor) がマウスを掴んでいる時は Mouse 入力を無視 (Input::UiWantsMouse で判定、 UI 操作優先)
/// pitch / distance は clamp で有限範囲に強制、 NaN / 巨大値での render crash を防ぐ

#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"

#include <cstdint>

namespace NS::Platform
{
    class Input;
}

namespace NS::Scene
{
    class GameObject;

    /// 編集モード free-fly 仮想カメラ。実カメラは持たず free-fly 姿勢を pose として返す
    /// mode 切替時に `SetActive(bool)` で on/off し、CameraBrain が選択して実カメラへ書く
    class EditorCameraComponent : public VirtualCameraComponent
    {
    public:
        EditorCameraComponent() noexcept;

        void SetInput(NS::Platform::Input* input) noexcept;

        void OnUpdate() override;
        /// free-fly の現在姿勢を返す (alpha は使わない)。Brain が選択時に実カメラへ書く
        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // Programmatic API (test / mode toggle で state save/restore)
        void SetYawPitch(float yaw, float pitch) noexcept;
        void SetCenter(NS::Math::Vector3 center) noexcept;
        void SetDistance(float distance) noexcept;

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] NS::Math::Vector3 Center() const noexcept { return m_center; }
        [[nodiscard]] NS::Math::Vector3 ComputeCameraPosition() const noexcept;

        // 直接 input 注入 (test 経路 / OnUpdate に頼らない外部制御)
        void ApplyOrbit(float yawDelta, float pitchDelta) noexcept;
        void ApplyPan(float panX, float panY) noexcept;
        void ApplyZoom(float zoomDelta) noexcept;

        // free-fly の感触を Inspector へ公開する。 毎フレーム読まれるのでライブで効く
        NS_REFLECT_BEGIN(EditorCameraComponent)
        NS_REFLECT_FIELD(m_springOmega, "Spring Omega")
        NS_REFLECT_FIELD(m_mouseSensOrbit, "Mouse Orbit Sens")
        NS_REFLECT_FIELD(m_mouseSensPan, "Mouse Pan Sens")
        NS_REFLECT_FIELD(m_mouseSensZoom, "Mouse Zoom Sens")
        NS_REFLECT_FIELD(m_padSensOrbit, "Pad Orbit Sens")
        NS_REFLECT_FIELD(m_padSensPan, "Pad Pan Sens")
        NS_REFLECT_FIELD(m_padSensZoom, "Pad Zoom Sens")
        NS_REFLECT_END()

        // 2m 未満: block 内側に入り描画破綻。30m 超: block が点になる
        static constexpr float kMinDistance = 2.0f;
        static constexpr float kMaxDistance = 30.0f;
        // ±90° は up/forward 平行で gimbal lock 寸前のため ±89° でクランプ
        static constexpr float kPitchMin = -1.553f; // -89°
        static constexpr float kPitchMax = +1.553f; // +89°

    private:
        NS::Platform::Input* m_input = nullptr;

        NS::Math::Vector3 m_center{0.0f, 0.0f, 0.0f};
        float m_yaw = 0.0f;
        float m_pitch = -0.5236f;
        float m_distance = 15.0f;
        float m_desiredDistance = 15.0f;
        float m_springOmega = 6.0f;

        float m_mouseSensOrbit = 0.003f;
        float m_mouseSensPan = 0.02f;
        // 1 wheel notch あたりの zoomDelta 倍率。 ApplyZoom が log scale なので
        // 1.0 で 1 notch = 10% 距離変化、 2.0 で 19%、 0.5 で 5% と直感的に効く
        float m_mouseSensZoom = 1.0f;
        float m_padSensOrbit = 2.5f;
        float m_padSensPan = 8.0f;
        float m_padSensZoom = 4.0f;
    };

} // namespace NS::Scene
