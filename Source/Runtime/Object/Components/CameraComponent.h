#pragma once

#include "Runtime/Core/CameraData.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Gfx
{
    class Renderer;
}

namespace NS::Obj
{
    struct CameraPose;

    //! @brief NS::Core::CameraData を値で内包する Component
    //! @details Getter / Setter は内包する CameraData への薄いラッパー
    //! 前方向の XZ 成分は PlayerInput がカメラ相対の移動入力に使う
    class CameraComponent : public Component
    {
    public:
        //! priority は LateUpdate 帯の後方 (+50)。全ての更新が終わった後に追う
        CameraComponent() noexcept;

        void SetPosition(const NS::Core::Vector3& position) noexcept;
        void SetTarget(const NS::Core::Vector3& target) noexcept;
        void SetUp(const NS::Core::Vector3& up) noexcept;
        void SetFovY(NS::Core::Radians fov) noexcept;
        void SetAspectRatio(float aspect) noexcept;
        void SetAspectRatioFromRenderer(const NS::Gfx::Renderer& renderer) noexcept;
        void SetNearPlane(float nearPlane) noexcept;
        void SetFarPlane(float farPlane) noexcept;

        //! pose の位置 / 注視点 / up と投影設定をまとめて書く。Brain の Evaluate とエディタの視点反映が使う
        void ApplyPose(const CameraPose& pose) noexcept;

        [[nodiscard]] const NS::Core::Vector3& Position() const noexcept { return m_camera.Position(); }
        [[nodiscard]] const NS::Core::Vector3& Target() const noexcept { return m_camera.Target(); }
        [[nodiscard]] const NS::Core::Vector3& Up() const noexcept { return m_camera.Up(); }
        [[nodiscard]] NS::Core::Radians FovY() const noexcept { return m_camera.FovY(); }

        //! 内包する CameraData への変更不可参照。skybox 描画とカメラ位置を読むのに使う
        [[nodiscard]] const NS::Core::CameraData& Camera() const noexcept { return m_camera; }
        [[nodiscard]] NS::Core::CameraData& Camera() noexcept { return m_camera; }

        [[nodiscard]] NS::Core::Matrix ViewProjection() const noexcept { return m_camera.ViewProjection(); }

        //! target - position を XZ 平面で正規化した前方向。距離 0 や Y 方向だけならワールドの +Z
        [[nodiscard]] NS::Core::Vector3 ForwardHorizontal() const noexcept;

        // pose は CameraBrain が毎フレーム上書きするので保存する調整値は無い。型名だけ登録する
        NS_REFLECT_NONE(CameraComponent, Component)

    private:
        NS::Core::CameraData m_camera; // 内包する実カメラ
    };
} // namespace NS::Obj
