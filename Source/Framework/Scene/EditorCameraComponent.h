#pragma once

/// @file EditorCameraComponent.h
/// @brief NS::Scene::EditorCameraComponent — 編集モード用 free-fly カメラ Component
///
/// @details Spherical 座標 (yaw, pitch, distance) + center pivot 表現
/// Mouse 右ドラッグ = Orbit、 中ドラッグ = Pan、 Wheel = Zoom
/// Gamepad 右スティック = Orbit、 左スティック = Pan、 LT-RT = Zoom
/// ImGui の `WantCaptureMouse() == true` の時は Mouse 入力を無視 (UI 操作優先)
/// pitch / distance は clamp で有限範囲に強制、 NaN / 巨大値での render crash を防ぐ

#include "Framework/Core/Math.h"
#include "Framework/Scene/Component.h"

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
    class CameraComponent;
    class GameObject;

    /// 編集モード free-fly camera。 ThirdPersonFollowComponent と並列の Component で、
    /// mode 切替時に `SetActive(bool)` で on/off する
    class EditorCameraComponent : public Component
    {
    public:
        explicit EditorCameraComponent(NS::Scene::GameObject* owner) noexcept;

        void SetCamera(CameraComponent* camera) noexcept;
        void SetInput(NS::Platform::Input* input) noexcept;
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept;

        void OnUpdate() override;

        // Programmatic API (test / mode toggle で state save/restore)
        void SetYawPitch(float yaw, float pitch) noexcept;
        void SetCenter(NS::Core::Vector3 center) noexcept;
        void SetDistance(float distance) noexcept;

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] NS::Core::Vector3 Center() const noexcept { return m_center; }
        [[nodiscard]] NS::Core::Vector3 ComputeCameraPosition() const noexcept;

        // 直接 input 注入 (test 経路 / OnUpdate に頼らない外部制御)
        void ApplyOrbit(float yawDelta, float pitchDelta) noexcept;
        void ApplyPan(float panX, float panY) noexcept;
        void ApplyZoom(float zoomDelta) noexcept;

        // 1m grid を一次対象とする vertical slice 想定で範囲を調整
        // 近すぎる (< 2m) と FOV 60° で block 内側に入って描画破綻、 遠すぎる (> 30m)
        // と block が点になるため 2 ~ 30m に絞る
        static constexpr float kMinDistance = 2.0f;
        static constexpr float kMaxDistance = 30.0f;
        // 編集 free-fly camera は真上 (top-down) ~ 真下 (under-view) まで自由に振れる
        // ようにする。 90° 直前は LookAt の up 軸と forward が平行になり gimbal lock
        // 寸前で計算が崩れるため ±89° で clamp する
        static constexpr float kPitchMin = -1.553f; // -89° (scene の真下から見上げる手前)
        static constexpr float kPitchMax = +1.553f; // +89° (scene の真上から見下ろす手前)

    private:
        CameraComponent* m_camera = nullptr;
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;

        NS::Core::Vector3 m_center{0.0f, 0.0f, 0.0f};
        float m_yaw = 0.0f;
        float m_pitch = -0.5236f;
        float m_distance = 15.0f;
        float m_desiredDistance = 15.0f;
        float m_springOmega = 6.0f;

        // 個人プロジェクト固定値。 今後 Settings UI 経由 tune 想定
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
        [[nodiscard]] NS::Core::Ray ScreenToWorldRay(const NS::Core::Matrix& viewProjection,
                                                     NS::Core::Size2D viewport,
                                                     int mouseX,
                                                     int mouseY) noexcept;

        /// world 座標を grid 中心に snap (最近接 cell center)
        [[nodiscard]] NS::Core::Vector3 SnapWorldPointToGrid(NS::Core::Vector3 worldPoint,
                                                             float gridSize = kGridSize) noexcept;

        /// AABB hit 結果から、 hit 面の法線方向に 1 grid offset した cell 中心を返す
        /// `hitNormal` は ±X / ±Y / ±Z のいずれか
        [[nodiscard]] NS::Core::Vector3 SnapHitToPlacementCell(NS::Core::Vector3 hitPoint,
                                                               NS::Core::Vector3 hitNormal,
                                                               float gridSize = kGridSize) noexcept;

        /// ray が ground plane (y = 0) と交わる点を grid snap して返す
        /// 上向き ray / 後方ヒットの場合は false (`outCellCenter` 未変更)
        [[nodiscard]] bool TryGroundPlaneFallback(const NS::Core::Ray& ray,
                                                  NS::Core::Vector3& outCellCenter,
                                                  float gridSize = kGridSize) noexcept;

        /// rotation u8 (0/1/2/3) を Y 軸 90° 単位の quaternion に変換する
        [[nodiscard]] NS::Core::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept;
    } // namespace EditorGridMath

} // namespace NS::Scene
