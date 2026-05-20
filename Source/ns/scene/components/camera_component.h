#pragma once

/// @file camera_component.h
/// @brief `ns::graphics::Camera` を value member で内包する Component。
///        Getter / Setter は基本的に内包 Camera への薄いラッパー。view forward (XZ) は
///        PlayerInput が camera 相対移動入力の参照に使う。

#include "ns/core/math.h"
#include "ns/graphics/camera.h"
#include "ns/scene/component.h"

namespace ns::graphics
{
    class Renderer;
}

namespace ns::scene
{
    class CameraComponent : public Component
    {
    public:
        CameraComponent() noexcept = default;

        void SetPosition(const ns::core::Vector3& position) noexcept;
        void SetTarget(const ns::core::Vector3& target) noexcept;
        void SetUp(const ns::core::Vector3& up) noexcept;
        void SetFovY(float radians) noexcept;
        void SetAspectRatio(float aspect) noexcept;
        void SetAspectRatioFromRenderer(const ns::graphics::Renderer& renderer) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        [[nodiscard]] const ns::core::Vector3& Position() const noexcept { return m_camera.Position(); }
        [[nodiscard]] const ns::core::Vector3& Target() const noexcept { return m_camera.Target(); }
        [[nodiscard]] const ns::core::Vector3& Up() const noexcept { return m_camera.Up(); }
        [[nodiscard]] float FovY() const noexcept { return m_camera.FovY(); }

        /// 内包 Camera への変更不可参照。MeshComponent::Draw に ViewProjection を渡す用途で使う。
        [[nodiscard]] const ns::graphics::Camera& Camera() const noexcept { return m_camera; }
        [[nodiscard]] ns::graphics::Camera& Camera() noexcept { return m_camera; }

        [[nodiscard]] ns::core::Matrix ViewProjection() const noexcept { return m_camera.ViewProjection(); }

        /// target - position を XZ 平面で正規化した forward。距離 0 / Y 方向のみの場合は world +Z。
        /// PlayerInput が camera 相対移動の前向きベクトルとして読む。
        [[nodiscard]] ns::core::Vector3 ForwardHorizontal() const noexcept;

    private:
        ns::graphics::Camera m_camera;
    };
} // namespace ns::scene
