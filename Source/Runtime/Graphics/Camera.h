#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Graphics
{

    //! @brief カメラの初期構築用パラメータ
    //! @details 構築後の値の変更は各種設定用関数を使用する
    struct CameraDesc
    {
        NS::Core::Vector3 position{0.0f, 0.0f, -5.0f};                         //!< カメラ位置
        NS::Core::Vector3 target{0.0f, 0.0f, 0.0f};                            //!< 注視点
        NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};                                //!< アップベクトル
        NS::Core::Radians fovY{NS::Core::ToRadians(NS::Core::Degrees{60.0f})}; //!< 垂直視野角
        float aspectRatio = 16.0f / 9.0f;                                      //!< アスペクト比
        float nearPlane = 0.1f;                                                //!< ニアクリップ距離
        float farPlane = 1000.0f;                                              //!< ファークリップ距離
    };

    //! @brief ビュー行列および投影行列の計算を担う、左手系かつ透視投影専用のクラス
    //! @details 描画システムやシーン管理への依存を持たない
    //! 設定変更時は再計算フラグを立てるのみとし、実際の行列計算は値の取得時まで遅延される
    class Camera
    {
    public:
        Camera() noexcept;

        //! 全パラメータを一括で設定し、設定漏れを防ぐ
        explicit Camera(const CameraDesc& desc) noexcept;

        Camera(const Camera&) = default;
        Camera& operator=(const Camera&) = default;
        Camera(Camera&&) = default;
        Camera& operator=(Camera&&) = default;
        ~Camera() = default;

        void SetPosition(const NS::Core::Vector3& position) noexcept;
        void SetTarget(const NS::Core::Vector3& target) noexcept;
        void SetUp(const NS::Core::Vector3& up) noexcept;

        //! 単位の取り違え事故を防ぐため、専用の角度型で受け取る
        void SetFovY(NS::Core::Radians fov) noexcept;
        //! 幅を高さで割った値を指定する。ウィンドウリサイズ時などは再設定が必要
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

        //! XMMatrixLookAtLH 相当
        [[nodiscard]] const NS::Core::Matrix& View() const noexcept;
        //! XMMatrixPerspectiveFovLH 相当
        [[nodiscard]] const NS::Core::Matrix& Projection() const noexcept;
        //! ビュー行列と投影行列の乗算結果を返す。左手系のため、シェーダー側では行ベクトル前提で乗算すること
        [[nodiscard]] NS::Core::Matrix ViewProjection() const noexcept;

    private:
        NS::Core::Vector3 m_position; //!< カメラ位置
        NS::Core::Vector3 m_target;   //!< 注視点
        NS::Core::Vector3 m_up;       //!< アップベクトル
        NS::Core::Radians m_fovY;     //!< 垂直視野角
        float m_aspect;               //!< アスペクト比
        float m_near;                 //!< ニアクリップ距離
        float m_far;                  //!< ファークリップ距離

        mutable NS::Core::Matrix m_view;       //!< ビュー行列のキャッシュ
        mutable NS::Core::Matrix m_projection; //!< プロジェクション行列のキャッシュ
        mutable bool m_viewDirty;              //!< 位置・注視点・Up が変わったら立つ
        mutable bool m_projDirty;              //!< 視野角・アスペクト・クリップ距離が変わったら立つ
    };

} // namespace NS::Graphics
