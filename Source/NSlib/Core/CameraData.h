#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{

    //! 左手系の透視投影。設定を変更すると、次の行列取得時に再計算する
    class CameraData
    {
    public:
        void SetPosition(const NS::Vector3& position) noexcept;
        void SetTarget(const NS::Vector3& target) noexcept;
        void SetUp(const NS::Vector3& up) noexcept;

        void SetFovY(NS::Radians fov) noexcept;
        //! 幅を高さで割った値を指定する。ウィンドウリサイズ時などは再設定が必要
        void SetAspectRatio(float aspect) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;
        //! 投影後の画面をずらす。画面の幅・高さを 2 とする座標で、右と上が正。ビュー行列は変わらない
        void SetScreenOffset(const NS::Vector2& offset) noexcept;

        [[nodiscard]] const NS::Vector3& Position() const noexcept;
        [[nodiscard]] const NS::Vector3& Target() const noexcept;
        [[nodiscard]] const NS::Vector3& Up() const noexcept;

        [[nodiscard]] NS::Radians FovY() const noexcept;
        [[nodiscard]] float AspectRatio() const noexcept;
        [[nodiscard]] float NearPlane() const noexcept;
        [[nodiscard]] float FarPlane() const noexcept;
        [[nodiscard]] const NS::Vector2& ScreenOffset() const noexcept;

        //! XMMatrixLookAtLH 相当。位置と注視点が重なっている時は単位行列を返す
        [[nodiscard]] const NS::Matrix& View() const noexcept;
        //! XMMatrixPerspectiveFovLH 相当
        [[nodiscard]] const NS::Matrix& Projection() const noexcept;
        //! ビュー行列と投影行列の乗算結果を返す。行ベクトル前提で View、Projection の順に掛ける
        [[nodiscard]] NS::Matrix ViewProjection() const noexcept;

    private:
        NS::Vector3 m_position{0.0f, 0.0f, -5.0f};
        NS::Vector3 m_target{0.0f, 0.0f, 0.0f};
        NS::Vector3 m_up{0.0f, 1.0f, 0.0f};
        NS::Radians m_fovY{NS::ToRadians(NS::Degrees{60.0f})};
        float m_aspect = 16.0f / 9.0f;
        float m_near = 0.1f;
        float m_far = 1000.0f;
        NS::Vector2 m_screenOffset{0.0f, 0.0f};

        mutable NS::Matrix m_view = NS::Matrix::Identity;
        mutable NS::Matrix m_projection = NS::Matrix::Identity;
        mutable bool m_viewDirty = true;
        mutable bool m_projDirty = true;
    };

} // namespace NS
