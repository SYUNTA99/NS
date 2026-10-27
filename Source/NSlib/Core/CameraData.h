#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{

    /// 左手系の透視投影カメラ。設定変更は次の行列取得時に遅延反映される
    class CameraData
    {
    public:
        void SetPosition(const Vector3& position) noexcept;
        void SetTarget(const Vector3& target) noexcept;
        void SetUp(const Vector3& up) noexcept;

        void SetFovY(Radians fov) noexcept;
        /// 幅 / 高さを指定する
        void SetAspectRatio(float aspect) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;
        /// 投影後の NDC 座標系（幅・高さ 2、右上正）での平行移動量。ビュー行列には影響しない
        void SetScreenOffset(const Vector2& offset) noexcept;

        [[nodiscard]] const Vector3& Position() const noexcept;
        [[nodiscard]] const Vector3& Target() const noexcept;
        [[nodiscard]] const Vector3& Up() const noexcept;

        [[nodiscard]] Radians FovY() const noexcept;
        [[nodiscard]] float AspectRatio() const noexcept;
        [[nodiscard]] float NearPlane() const noexcept;
        [[nodiscard]] float FarPlane() const noexcept;
        [[nodiscard]] const Vector2& ScreenOffset() const noexcept;

        /// 位置と注視点が一致している場合は単位行列を返す
        [[nodiscard]] const Matrix& View() const noexcept;
        [[nodiscard]] const Matrix& Projection() const noexcept;
        /// 行ベクトル規約に基づき View * Projection を返す
        [[nodiscard]] Matrix ViewProjection() const noexcept;

    private:
        Vector3 m_position{0.0f, 0.0f, -5.0f};
        Vector3 m_target{0.0f, 0.0f, 0.0f};
        Vector3 m_up{0.0f, 1.0f, 0.0f};
        Radians m_fovY{ToRadians(Degrees{60.0f})};
        float m_aspect = 16.0f / 9.0f;
        float m_near = 0.1f;
        float m_far = 1000.0f;
        Vector2 m_screenOffset{0.0f, 0.0f};

        mutable Matrix m_view = Matrix::Identity;
        mutable Matrix m_projection = Matrix::Identity;
        mutable bool m_viewDirty = true;
        mutable bool m_projDirty = true;
    };

} // namespace NS
