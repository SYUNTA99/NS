#pragma once

#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Math.h"

namespace NS::Gfx
{
        class D3DDevice;
} // namespace NS::Gfx

namespace NS
{
    struct CameraPose;

    //! シーンの実カメラ。描画と編集用で、遊びの向きは IGetCamera から取得する
    class SceneCamera
    {
    public:
        SceneCamera() noexcept = default;

        void SetPosition(const Vector3& position) noexcept;
        void SetTarget(const Vector3& target) noexcept;
        void SetUp(const Vector3& up) noexcept;
        void SetAspectRatioFromDevice(const Gfx::D3DDevice& device) noexcept;

        //! pose の位置 / 注視点 / up と投影設定をまとめて書く。CameraManager の Evaluate とエディタの視点反映が使う
        void ApplyPose(const CameraPose& pose) noexcept;

        [[nodiscard]] const Vector3& Position() const noexcept { return m_camera.Position(); }
        [[nodiscard]] const Vector3& Target() const noexcept { return m_camera.Target(); }
        [[nodiscard]] const Vector3& Up() const noexcept { return m_camera.Up(); }
        [[nodiscard]] Radians FovY() const noexcept { return m_camera.FovY(); }

        //! 内包する CameraData への変更不可参照。skybox 描画とカメラ位置を読むのに使う
        [[nodiscard]] const CameraData& Camera() const noexcept { return m_camera; }

        [[nodiscard]] Matrix ViewProjection() const noexcept { return m_camera.ViewProjection(); }

    private:
        CameraData m_camera; // 内包する実カメラ
    };
} // namespace NS
