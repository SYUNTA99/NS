#pragma once

/// @file Camera.h
/// @brief NS::Graphics::Camera — View + Projection 行列を提供する Plain Class。
///
/// @details GPU リソース所有なし、 Renderer / Scene 依存なし。 CameraComponent から
/// 将来内包される予定。 座標系は LH 一本 ( / )、 Up = (0,1,0) 既定、
/// Perspective のみ。 垂直 FOV は強い型 `NS::Core::Radians`、 setter で
/// 内部の dirty フラグを立て、 Getter で初めて行列再計算するレイジー方式。

#include "Framework/Core/Math.h"

namespace NS::Graphics
{

    /// View + Projection 行列を提供する Plain Class。
    /// GPU リソース所有なし、Renderer/Scene 依存なし。
    /// CameraComponent から将来内包される予定。
    /// 座標系は LH 一本 (/)、Up = (0,1,0) 既定、Perspective のみ。
    /// setter で内部の dirty フラグが立ち、Getter で初めて行列再計算するレイジー方式。
    class Camera
    {
    public:
        Camera() noexcept;

        Camera(const Camera&) = default;
        Camera& operator=(const Camera&) = default;
        Camera(Camera&&) = default;
        Camera& operator=(Camera&&) = default;
        ~Camera() = default;

        void SetPosition(const NS::Core::Vector3& position) noexcept;
        void SetTarget(const NS::Core::Vector3& target) noexcept;
        void SetUp(const NS::Core::Vector3& up) noexcept;

        /// 垂直 FOV を強い型 Radians で受ける。 raw float の取り違え事故を防ぐ。
        void SetFovY(NS::Core::Radians fov) noexcept;
        /// アスペクト比 (width / height)。Window リサイズ時に呼出責任は Game 側。
        void SetAspectRatio(float aspect) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        [[nodiscard]] const NS::Core::Vector3& Position() const noexcept;
        [[nodiscard]] const NS::Core::Vector3& Target() const noexcept;
        [[nodiscard]] const NS::Core::Vector3& Up() const noexcept;

        [[nodiscard]] NS::Core::Radians FovY() const noexcept;
        [[nodiscard]] float AspectRatio() const noexcept;
        [[nodiscard]] float NearPlane() const noexcept;
        [[nodiscard]] float FarPlane() const noexcept;

        /// XMMatrixLookAtLH 相当。dirty 時のみ再計算しキャッシュ。
        [[nodiscard]] const NS::Core::Matrix& View() const noexcept;
        /// XMMatrixPerspectiveFovLH 相当。dirty 時のみ再計算しキャッシュ。
        [[nodiscard]] const NS::Core::Matrix& Projection() const noexcept;
        /// View() * Projection() を返す (DirectXMath row-major LH 慣習)。
        /// HLSL 側は `mul(float4(pos,1), ViewProjection)` の行ベクトル前提で書く。
        [[nodiscard]] NS::Core::Matrix ViewProjection() const noexcept;

    private:
        NS::Core::Vector3 m_position;
        NS::Core::Vector3 m_target;
        NS::Core::Vector3 m_up;
        NS::Core::Radians m_fovY;
        float m_aspect;
        float m_near;
        float m_far;

        mutable NS::Core::Matrix m_view;
        mutable NS::Core::Matrix m_projection;
        mutable bool m_viewDirty;
        mutable bool m_projDirty;
    };

} // namespace NS::Graphics
