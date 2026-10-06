#pragma once

#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Math.h"

namespace NS::Gfx
{
    class Renderer;
}

namespace NS::Obj
{
    struct CameraPose;

    //! @brief Scene が 1 つ持つ実カメラ。NS::CameraData を値で内包する
    //! @details Getter / Setter は内包する CameraData への薄いラッパー
    //! 描画とエディタが書いて読む。遊びは読まず、向きと位置は IUseCamera の補助関数から引く
    class SceneCamera
    {
    public:
        SceneCamera() noexcept = default;

        void SetPosition(const NS::Vector3& position) noexcept;
        void SetTarget(const NS::Vector3& target) noexcept;
        void SetUp(const NS::Vector3& up) noexcept;
        void SetFovY(NS::Radians fov) noexcept;
        void SetAspectRatio(float aspect) noexcept;
        void SetAspectRatioFromRenderer(const NS::Gfx::Renderer& renderer) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        //! pose の位置 / 注視点 / up と投影設定をまとめて書く。CameraManager の Evaluate とエディタの視点反映が使う
        void ApplyPose(const CameraPose& pose) noexcept;

        [[nodiscard]] const NS::Vector3& Position() const noexcept { return m_camera.Position(); }
        [[nodiscard]] const NS::Vector3& Target() const noexcept { return m_camera.Target(); }
        [[nodiscard]] const NS::Vector3& Up() const noexcept { return m_camera.Up(); }
        [[nodiscard]] NS::Radians FovY() const noexcept { return m_camera.FovY(); }

        //! 内包する CameraData への変更不可参照。skybox 描画とカメラ位置を読むのに使う
        [[nodiscard]] const NS::CameraData& Camera() const noexcept { return m_camera; }
        [[nodiscard]] NS::CameraData& Camera() noexcept { return m_camera; }

        [[nodiscard]] NS::Matrix ViewProjection() const noexcept { return m_camera.ViewProjection(); }

    private:
        NS::CameraData m_camera; // 内包する実カメラ
    };
} // namespace NS::Obj
