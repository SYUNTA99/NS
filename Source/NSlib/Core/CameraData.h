#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{

    //! @brief 左手系・透視投影のビュー行列と投影行列を作るクラス
    //! @details Core 層に閉じていて Graphics / Object 層に依存しない
    //! 設定の変更では再計算の印を立てるだけで、行列は取得時に作る
    class CameraData
    {
    public:
        void SetPosition(const NS::Vector3& position) noexcept;
        void SetTarget(const NS::Vector3& target) noexcept;
        void SetUp(const NS::Vector3& up) noexcept;

        //! 単位の取り違え事故を防ぐため、専用の角度型で受け取る
        void SetFovY(NS::Radians fov) noexcept;
        //! 幅を高さで割った値を指定する。ウィンドウリサイズ時などは再設定が必要
        void SetAspectRatio(float aspect) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;
        //! @brief 射影の後の画面を offset だけずらす。縦横とも画面の幅を 2 とした座標で、右と上が正
        //! @details 投影行列の 3 行目の x と y に足す。どの奥行きの点も同じだけずれ、ビュー行列は変わらない
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
        NS::Vector3 m_position{0.0f, 0.0f, -5.0f};             //!< カメラ位置
        NS::Vector3 m_target{0.0f, 0.0f, 0.0f};                //!< 注視点
        NS::Vector3 m_up{0.0f, 1.0f, 0.0f};                    //!< アップベクトル
        NS::Radians m_fovY{NS::ToRadians(NS::Degrees{60.0f})}; //!< 垂直視野角
        float m_aspect = 16.0f / 9.0f;                         //!< アスペクト比
        float m_near = 0.1f;                                   //!< ニアクリップ距離
        float m_far = 1000.0f;                                 //!< ファークリップ距離
        NS::Vector2 m_screenOffset{0.0f, 0.0f};                //!< 射影の後の画面のずれ

        mutable NS::Matrix m_view = NS::Matrix::Identity;       //!< ビュー行列のキャッシュ
        mutable NS::Matrix m_projection = NS::Matrix::Identity; //!< プロジェクション行列のキャッシュ
        mutable bool m_viewDirty = true;                        //!< 位置・注視点・Up が変わったら立つ
        mutable bool m_projDirty = true;                        //!< 視野角・アスペクト・クリップ距離が変わったら立つ
    };

} // namespace NS
