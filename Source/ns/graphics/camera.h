#pragma once

#include "ns/core/math.h"

namespace ns::graphics
{

    /// View + Projection 行列を提供する Plain Class。
    /// GPU リソース所有なし、Renderer/Scene 依存なし。
    ///  で CameraComponent から内包される予定。
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

        void SetPosition(const ns::core::Vector3& position) noexcept;
        void SetTarget(const ns::core::Vector3& target) noexcept;
        void SetUp(const ns::core::Vector3& up) noexcept;

        /// 垂直 FOV (ラジアン)。
        void SetFovY(float radians) noexcept;
        /// アスペクト比 (width / height)。Window リサイズ時に呼出責任は Game 側。
        void SetAspectRatio(float aspect) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        [[nodiscard]] const ns::core::Vector3& Position() const noexcept;
        [[nodiscard]] const ns::core::Vector3& Target() const noexcept;
        [[nodiscard]] const ns::core::Vector3& Up() const noexcept;

        [[nodiscard]] float FovY() const noexcept;
        [[nodiscard]] float AspectRatio() const noexcept;
        [[nodiscard]] float NearPlane() const noexcept;
        [[nodiscard]] float FarPlane() const noexcept;

        /// XMMatrixLookAtLH 相当。dirty 時のみ再計算しキャッシュ。
        [[nodiscard]] const ns::core::Matrix& View() const noexcept;
        /// XMMatrixPerspectiveFovLH 相当。dirty 時のみ再計算しキャッシュ。
        [[nodiscard]] const ns::core::Matrix& Projection() const noexcept;
        /// View() * Projection() を返す (DirectXMath row-major LH 慣習)。
        /// HLSL 側は `mul(float4(pos,1), ViewProjection)` の行ベクトル前提で書く。
        [[nodiscard]] ns::core::Matrix ViewProjection() const noexcept;

    private:
        ns::core::Vector3 m_position;
        ns::core::Vector3 m_target;
        ns::core::Vector3 m_up;
        float m_fovY;
        float m_aspect;
        float m_near;
        float m_far;

        mutable ns::core::Matrix m_view;
        mutable ns::core::Matrix m_projection;
        mutable bool m_viewDirty;
        mutable bool m_projDirty;
    };

} // namespace ns::graphics
