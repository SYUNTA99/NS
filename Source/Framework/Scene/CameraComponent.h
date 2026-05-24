#pragma once

/// @file CameraComponent.h
/// @brief `NS::Graphics::Camera` を value member で内包する Component。
///        Getter / Setter は基本的に内包 Camera への薄いラッパー。view forward (XZ) は
///        PlayerInput が camera 相対移動入力の参照に使う。

#include "Framework/Core/Math.h"
#include "Framework/Graphics/Camera.h"
#include "Framework/Scene/Component.h"

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Scene
{
    class CameraComponent : public Component
    {
    public:
        CameraComponent() noexcept = default;

        /// GameObject owner を受け取って auto-register する ctor。priority は Camera 帯 (400) で
        /// base ctor に渡される (follow 系処理を Input / Physics 帯の後に走らせるため)。
        explicit CameraComponent(NS::Scene::GameObject* owner) noexcept;

        void SetPosition(const NS::Core::Vector3& position) noexcept;
        void SetTarget(const NS::Core::Vector3& target) noexcept;
        void SetUp(const NS::Core::Vector3& up) noexcept;
        void SetFovY(NS::Core::Radians fov) noexcept;
        void SetAspectRatio(float aspect) noexcept;
        void SetAspectRatioFromRenderer(const NS::Graphics::Renderer& renderer) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        [[nodiscard]] const NS::Core::Vector3& Position() const noexcept { return m_camera.Position(); }
        [[nodiscard]] const NS::Core::Vector3& Target() const noexcept { return m_camera.Target(); }
        [[nodiscard]] const NS::Core::Vector3& Up() const noexcept { return m_camera.Up(); }
        [[nodiscard]] NS::Core::Radians FovY() const noexcept { return m_camera.FovY(); }

        /// 内包 Camera への変更不可参照。MeshComponent::Draw に ViewProjection を渡す用途で使う。
        [[nodiscard]] const NS::Graphics::Camera& Camera() const noexcept { return m_camera; }
        [[nodiscard]] NS::Graphics::Camera& Camera() noexcept { return m_camera; }

        [[nodiscard]] NS::Core::Matrix ViewProjection() const noexcept { return m_camera.ViewProjection(); }

        /// target - position を XZ 平面で正規化した forward。距離 0 / Y 方向のみの場合は world +Z。
        /// PlayerInput が camera 相対移動の前向きベクトルとして読む。
        [[nodiscard]] NS::Core::Vector3 ForwardHorizontal() const noexcept;

    private:
        NS::Graphics::Camera m_camera;
    };
} // namespace NS::Scene
