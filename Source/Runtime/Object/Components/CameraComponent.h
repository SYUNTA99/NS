#pragma once

#include "Runtime/Graphics/Camera.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Object
{
    /// @brief NS::Graphics::Camera を value member で内包する Component
    /// @details Getter / Setter は内包 Camera への薄いラッパー
    /// view forward の XZ 成分は PlayerInput が camera 相対移動入力の参照に使う
    class CameraComponent : public Component
    {
    public:
        /// priority は LateUpdate 帯の後方 (+50)。全ての更新が終わった後に追う
        CameraComponent() noexcept;

        void SetPosition(const NS::Math::Vector3& position) noexcept;
        void SetTarget(const NS::Math::Vector3& target) noexcept;
        void SetUp(const NS::Math::Vector3& up) noexcept;
        void SetFovY(NS::Math::Radians fov) noexcept;
        void SetAspectRatio(float aspect) noexcept;
        void SetAspectRatioFromRenderer(const NS::Graphics::Renderer& renderer) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        [[nodiscard]] const NS::Math::Vector3& Position() const noexcept { return m_camera.Position(); }
        [[nodiscard]] const NS::Math::Vector3& Target() const noexcept { return m_camera.Target(); }
        [[nodiscard]] const NS::Math::Vector3& Up() const noexcept { return m_camera.Up(); }
        [[nodiscard]] NS::Math::Radians FovY() const noexcept { return m_camera.FovY(); }

        /// 内包 Camera への変更不可参照。skybox 描画とカメラ位置を読むのに使う
        [[nodiscard]] const NS::Graphics::Camera& Camera() const noexcept { return m_camera; }
        [[nodiscard]] NS::Graphics::Camera& Camera() noexcept { return m_camera; }

        [[nodiscard]] NS::Math::Matrix ViewProjection() const noexcept { return m_camera.ViewProjection(); }

        /// target-position を XZ 正規化した forward。距離0 / Y 方向のみなら world +Z。PlayerInput の camera 相対移動用
        [[nodiscard]] NS::Math::Vector3 ForwardHorizontal() const noexcept;

        // pose は CameraBrain が毎フレーム上書きするので保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(CameraComponent, Component)

    private:
        NS::Graphics::Camera m_camera; // 内包する実カメラ
    };
} // namespace NS::Object
