#pragma once

/// @file EditorCameraComponent.h
/// @brief NS::Scene::EditorCameraComponent — 編集モード用 free-fly カメラ Component
///
/// @details Spherical 座標 (yaw, pitch, distance) + center pivot 表現
/// Mouse 右ドラッグ = Orbit、 中ドラッグ = Pan、 Wheel = Zoom
/// Gamepad 右スティック = Orbit、 左スティック = Pan、 LT-RT = Zoom
/// ImGui の `WantCaptureMouse() == true` の時は Mouse 入力を無視 (UI 操作優先)
/// pitch / distance は clamp で有限範囲に強制、 NaN / 巨大値での render crash を防ぐ

#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"

#include <cstdint>

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
    class GameObject;

    /// 編集モード free-fly 仮想カメラ。実カメラは持たず free-fly 姿勢を pose として返す
    /// mode 切替時に `SetActive(bool)` で on/off し、CameraBrain が選択して実カメラへ書く
    class EditorCameraComponent : public VirtualCameraComponent
    {
    public:
        EditorCameraComponent() noexcept;

        void SetInput(NS::Platform::Input* input) noexcept;
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept;

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

        // 2m 未満: block 内側に入り描画破綻。30m 超: block が点になる
        static constexpr float kMinDistance = 2.0f;
        static constexpr float kMaxDistance = 30.0f;
        // ±90° は up/forward 平行で gimbal lock 寸前のため ±89° でクランプ
        static constexpr float kPitchMin = -1.553f; // -89°
        static constexpr float kPitchMax = +1.553f; // +89°

    private:
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;

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

    /// 編集モード用 grid 数学 helper。 Component ではなく自由関数として配置し、
    /// EditorMode や Place / Delete / Rotate Command から再利用する
    namespace EditorGridMath
    {
        /// 1 grid サイズ (世界座標 1.0 m)
        constexpr float kGridSize = 1.0f;

        /// マウス screen 座標から world ray を生成する
        /// `viewProjection` は camera の VP matrix、 viewport は backbuffer サイズ
        [[nodiscard]] NS::Math::Ray ScreenToWorldRay(const NS::Math::Matrix& viewProjection,
                                                     NS::Math::Size2D viewport,
                                                     int mouseX,
                                                     int mouseY) noexcept;

        /// world 座標を grid 中心に snap (最近接 cell center)
        [[nodiscard]] NS::Math::Vector3 SnapWorldPointToGrid(NS::Math::Vector3 worldPoint,
                                                             float gridSize = kGridSize) noexcept;

        /// AABB hit 結果から、 hit 面の法線方向に 1 grid offset した cell 中心を返す
        /// `hitNormal` は ±X / ±Y / ±Z のいずれか
        [[nodiscard]] NS::Math::Vector3 SnapHitToPlacementCell(NS::Math::Vector3 hitPoint,
                                                               NS::Math::Vector3 hitNormal,
                                                               float gridSize = kGridSize) noexcept;

        /// ray が ground plane (y = 0) と交わる点を grid snap して返す
        /// 上向き ray / 後方ヒットの場合は false (`outCellCenter` 未変更)
        [[nodiscard]] bool TryGroundPlaneFallback(const NS::Math::Ray& ray,
                                                  NS::Math::Vector3& outCellCenter,
                                                  float gridSize = kGridSize) noexcept;

        /// rotation u8 (0/1/2/3) を Y 軸 90° 単位の quaternion に変換する
        [[nodiscard]] NS::Math::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept;
    } // namespace EditorGridMath

} // namespace NS::Scene
